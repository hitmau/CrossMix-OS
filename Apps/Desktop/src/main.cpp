#include <SDL.h>
#include <SDL_ttf.h>

#include "device_status.hpp"
#include "right_stick_pointer.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr int W = 1280;
constexpr int H = 720;

struct Color { Uint8 r, g, b, a; };
const Color ink{13, 19, 35, 255};
const Color white{243, 246, 255, 255};
const Color muted{160, 174, 199, 255};
const Color blue{85, 157, 255, 255};
const Color violet{158, 124, 255, 255};
const Color green{91, 207, 166, 255};

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
std::vector<TTF_Font*> fonts;
std::vector<SDL_Joystick*> joysticks;
std::string appDir, dataDir, rootDir, workspaceDir;
std::string editorText, activeName = "hello.py";
std::size_t caret = 0;
std::string consoleText;
std::string modalTitle, modalBody;
bool running = true, dirty = true, startMenu = false, editorOpen = false;
bool oskOpen = false, modalOpen = false;
bool browserOpen = false, browserPageView = false, browserInputFocused = false;
bool menuSearchFocused = false, consolePlatform = true, browserLoading = false;
bool usePointerForConfirm = false;
int menuSelection = 0, focus = 0, oskSelection = 0;
int cursorX = W / 2, cursorY = H / 2;
int leftAxisX = 0, leftAxisY = 0;
std::size_t queryCaret = 0;
std::string menuQuery, browserQuery, browserStatus, browserPageTitle, browserPageText;
struct BrowserResult { std::string url, title, snippet; };
struct BrowserLink { std::string url, label; };
std::vector<BrowserResult> browserResults;
std::vector<BrowserLink> browserLinks;
int browserSelection = 0, browserScroll = 0, browserResultsOffset = 0;
enum class ChildTask { None, Program, BrowserSearch, BrowserOpen };
ChildTask childTask = ChildTask::None;
std::string childLineBuffer;
desktop_input::RightStickPointer rightStick{W, H};
desktop_status::Reader statusReader;
desktop_status::Snapshot deviceStatus;
pid_t childPid = -1;
int childOutput = -1;
uint64_t lastClockDraw = 0, lastAnalogNav = 0;
unsigned int lastPadRepeat = 0;
std::string capturePath;
bool captured = false;

std::string env(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

std::string path(const std::string& folder, const std::string& name) {
    return folder + (folder.empty() || folder.back() == '/' ? "" : "/") + name;
}

void color(Color c) { SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a); }

void fill(int x, int y, int w, int h, Color c) {
    SDL_Rect r{x, y, w, h}; color(c); SDL_RenderFillRect(renderer, &r);
}

void outline(int x, int y, int w, int h, Color c) {
    SDL_Rect r{x, y, w, h}; color(c); SDL_RenderDrawRect(renderer, &r);
}

TTF_Font* font(int size) {
    if (size >= static_cast<int>(fonts.size())) fonts.resize(size + 1, nullptr);
    if (!fonts[size]) fonts[size] = TTF_OpenFont(path(appDir, "font.ttf").c_str(), size);
    return fonts[size];
}

void drawText(const std::string& value, int x, int y, int size, Color c) {
    if (value.empty()) return;
    TTF_Font* f = font(size);
    if (!f) return;
    SDL_Color fg{c.r, c.g, c.b, c.a};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(f, value.c_str(), fg);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

int textWidth(const std::string& value, int size) {
    TTF_Font* f = font(size);
    int w = 0, h = 0;
    if (f) TTF_SizeUTF8(f, value.c_str(), &w, &h);
    return w;
}

std::string clockText() {
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    char buf[64]{};
    if (local) std::strftime(buf, sizeof(buf), "%H:%M  %d/%m", local);
    return buf;
}

std::string filePath(const std::string& name) { return path(workspaceDir, name); }

bool safeWorkspaceName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 100) return false;
    for (unsigned char ch : name) {
        if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == ' ')) return false;
    }
    return name.find("..") == std::string::npos;
}

void seedWorkspace() {
    const std::string examples = path(path(rootDir, "Data/Desktop"), "Workspace");
    for (const char* name : {"hello.py", "hello.js"}) {
        const std::string target = filePath(name);
        struct stat info {};
        if (stat(target.c_str(), &info) == 0) continue;
        std::ifstream in(path(examples, name), std::ios::binary);
        std::ofstream out(target, std::ios::binary);
        if (in && out) out << in.rdbuf();
    }
}

std::vector<std::string> workspaceFiles() {
    std::vector<std::string> result;
    DIR* directory = opendir(workspaceDir.c_str());
    if (!directory) return result;
    while (dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (!safeWorkspaceName(name)) continue;
        const std::string full = filePath(name);
        struct stat info {};
        if (stat(full.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
        const std::size_t dot = name.find_last_of('.');
        const std::string ext = dot == std::string::npos ? "" : name.substr(dot);
        if (ext == ".py" || ext == ".js" || ext == ".txt" || ext == ".md") result.push_back(name);
    }
    closedir(directory);
    std::sort(result.begin(), result.end());
    if (result.size() > 20) result.resize(20);
    return result;
}

struct MenuItem { const char* label; const char* detail; const char* glyph; };
const std::array<MenuItem, 6>& menuItems() {
    static const std::array<MenuItem, 6> items{{
        {"Internet", "Pesquisa e leitura de paginas", "W"},
        {"Editor de codigo", "Workspace Python e JavaScript", "</>"},
        {"Arquivos", "Dingux Commander", "F"},
        {"Terminal", "Shell do CrossMix", ">_"},
        {"Executar Python", "Abrir o exemplo no editor", "Py"},
        {"Voltar ao CrossMix", "Fechar o Desktop", "↩"}
    }};
    return items;
}

std::vector<int> menuMatches() {
    std::vector<int> matches;
    std::string query = menuQuery;
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (int i = 0; i < static_cast<int>(menuItems().size()); ++i) {
        std::string label = menuItems()[i].label;
        std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (query.empty() || label.find(query) != std::string::npos) matches.push_back(i);
    }
    return matches;
}

std::string gibibytes(std::uint64_t bytes, int decimals = 1) {
    if (!bytes) return "--";
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), decimals ? "%.1fG" : "%.0fG",
                  static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return buffer;
}

void appendQueryText(std::string& value, std::size_t& position, const char* text) {
    if (!text || !*text || value.size() >= 240) return;
    std::string add(text);
    add.resize(std::min<std::size_t>(add.size(), 240 - value.size()));
    position = std::min(position, value.size());
    value.insert(position, add);
    position += add.size();
    dirty = true;
}

void activate(int item);
void drawOsk();

void showModal(const std::string& title, const std::string& body) {
    modalTitle = title;
    modalBody = body;
    modalOpen = true;
    dirty = true;
}

void cancelBrowserTask();

void loadFile(const std::string& name) {
    if (!safeWorkspaceName(name)) { showModal("Nome de arquivo invalido", "Use um nome simples dentro do Workspace."); return; }
    activeName = name;
    std::ifstream in(filePath(name), std::ios::binary);
    if (in) editorText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    else editorText.clear();
    caret = editorText.size();
    consoleText.clear();
    dirty = true;
}

bool saveFile() {
    std::ofstream out(filePath(activeName), std::ios::binary | std::ios::trunc);
    if (!out) {
        showModal("Falha ao salvar", "Nao foi possivel gravar no workspace do Desktop.");
        return false;
    }
    out.write(editorText.data(), static_cast<std::streamsize>(editorText.size()));
    consoleText = "Arquivo salvo: Data/Desktop/Workspace/" + activeName;
    dirty = true;
    return true;
}

void openEditor(const std::string& name = "hello.py") {
    cancelBrowserTask();
    startMenu = false;
    browserOpen = false;
    editorOpen = true;
    oskOpen = false;
    menuSearchFocused = false;
    loadFile(name);
    SDL_StartTextInput();
}

void closeEditor() {
    if (childPid > 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, nullptr, 0);
        childPid = -1;
        childTask = ChildTask::None;
    }
    if (childOutput >= 0) { close(childOutput); childOutput = -1; }
    editorOpen = false;
    oskOpen = false;
    SDL_StopTextInput();
    dirty = true;
}

void cancelBrowserTask() {
    if (childPid > 0 && (childTask == ChildTask::BrowserSearch || childTask == ChildTask::BrowserOpen)) {
        kill(childPid, SIGTERM);
        waitpid(childPid, nullptr, 0);
        childPid = -1;
        childTask = ChildTask::None;
    }
    if (childOutput >= 0 && childTask == ChildTask::None) { close(childOutput); childOutput = -1; }
    childLineBuffer.clear();
    browserLoading = false;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t next = line.find('\t', start);
        fields.push_back(line.substr(start, next == std::string::npos ? std::string::npos : next - start));
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return fields;
}

void handleBrowserRecord(const std::string& line) {
    const auto fields = splitTabs(line);
    if (fields.empty()) return;
    if (fields[0] == "RESULT" && fields.size() >= 4) {
        browserResults.push_back(BrowserResult{fields[1], fields[2], fields[3]});
        browserStatus = "Resultados da pesquisa";
    } else if (fields[0] == "TITLE" && fields.size() >= 2) {
        browserPageTitle = fields[1];
    } else if (fields[0] == "TEXT" && fields.size() >= 2 && browserPageText.size() < 12000) {
        if (!browserPageText.empty()) browserPageText.push_back('\n');
        browserPageText += fields[1];
    } else if (fields[0] == "LINK" && fields.size() >= 3 && browserLinks.size() < 30) {
        browserLinks.push_back(BrowserLink{fields[1], fields[2]});
    } else if (fields[0] == "DONE") {
        browserLoading = false;
        if (childTask == ChildTask::BrowserSearch && browserResults.empty()) browserStatus = "Nenhum resultado. Tente outra busca.";
        else if (childTask == ChildTask::BrowserOpen && browserPageText.empty()) browserStatus = "A pagina nao trouxe texto legivel.";
        else if (childTask == ChildTask::BrowserOpen) browserStatus = "Pagina carregada";
    } else if (fields[0] == "ERROR") {
        browserLoading = false;
        browserStatus = fields.size() > 1 ? fields[1] : "Falha de rede";
    }
    dirty = true;
}

void runAppScript(const std::string& appName) {
    const std::string script = path(path(rootDir, "Apps/" + appName), "launch.sh");
    if (env("CROSSMIX_DESKTOP_PLATFORM", "console") == "preview") {
        showModal(appName == "Terminal" ? "Terminal" : "Arquivos",
                  "Este aplicativo do CrossMix so abre dentro do TrimUI.\nA previa no PC demonstra a interface do Desktop.");
        return;
    }
    SDL_HideWindow(window);
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", script.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    if (pid > 0) waitpid(pid, nullptr, 0);
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    dirty = true;
}

void startProgram(const std::string& program, const std::string& file) {
    if (childPid > 0) {
        showModal("Processo ativo", "Feche ou aguarde o processo atual antes de executar outro.");
        return;
    }
    int fds[2];
    if (pipe(fds) != 0) { showModal("Execucao", "Falha ao criar o canal de saida."); return; }
    childPid = fork();
    if (childPid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        const std::string python = env("CROSSMIX_DESKTOP_PYTHON", "/mnt/SDCARD/System/bin/python3.11");
        const std::string node = path(rootDir, "System/bin/node");
        const std::string executable = program == "python" ? python : node;
        char* args[] = {const_cast<char*>(executable.c_str()), const_cast<char*>(file.c_str()), nullptr};
        execvp(executable.c_str(), args);
        const char* fallback = program == "python" ? "python3" : "node";
        char* fallbackArgs[] = {const_cast<char*>(fallback), const_cast<char*>(file.c_str()), nullptr};
        execvp(fallback, fallbackArgs);
        const char* error = program == "python" ? "Python nao encontrado no PATH do CrossMix.\n" : "Node.js nao instalado.\n";
        write(STDERR_FILENO, error, std::strlen(error));
        _exit(127);
    }
    close(fds[1]);
    if (childPid < 0) {
        close(fds[0]); childPid = -1;
        showModal("Execucao", "Nao foi possivel iniciar o processo.");
        return;
    }
    childOutput = fds[0];
    fcntl(childOutput, F_SETFL, fcntl(childOutput, F_GETFL, 0) | O_NONBLOCK);
    childTask = ChildTask::Program;
    consoleText = "Executando " + activeName + "...\n";
    dirty = true;
}

void runCurrentFile() {
    if (!saveFile()) return;
    if (activeName.size() >= 3 && activeName.substr(activeName.size() - 3) == ".py")
        startProgram("python", filePath(activeName));
    else if (activeName.size() >= 3 && activeName.substr(activeName.size() - 3) == ".js")
        startProgram("node", filePath(activeName));
}

void startBrowserTask(const std::string& action, const std::string& argument) {
    if (childPid > 0) {
        showModal("Processo ativo", "Aguarde a tarefa atual terminar antes de iniciar outra.");
        return;
    }
    int fds[2];
    if (pipe(fds) != 0) { browserStatus = "Nao foi possivel criar o canal de rede."; dirty = true; return; }
    childPid = fork();
    if (childPid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        const std::string python = env("CROSSMIX_DESKTOP_PYTHON", "/mnt/SDCARD/System/bin/python3");
        const std::string backend = path(appDir, "browser_backend.py");
        char* args[] = {const_cast<char*>(python.c_str()), const_cast<char*>(backend.c_str()),
                        const_cast<char*>(action.c_str()), const_cast<char*>(argument.c_str()), nullptr};
        execvp(python.c_str(), args);
        char* fallback[] = {const_cast<char*>("python3"), const_cast<char*>(backend.c_str()),
                            const_cast<char*>(action.c_str()), const_cast<char*>(argument.c_str()), nullptr};
        execvp("python3", fallback);
        const char* message = "Python nao encontrado para iniciar o navegador.\n";
        write(STDERR_FILENO, message, std::strlen(message));
        _exit(127);
    }
    close(fds[1]);
    if (childPid < 0) {
        close(fds[0]); childPid = -1;
        browserStatus = "Nao foi possivel iniciar a tarefa de rede.";
        dirty = true;
        return;
    }
    childOutput = fds[0];
    fcntl(childOutput, F_SETFL, fcntl(childOutput, F_GETFL, 0) | O_NONBLOCK);
    childLineBuffer.clear();
    childTask = action == "search" ? ChildTask::BrowserSearch : ChildTask::BrowserOpen;
    browserLoading = true;
    dirty = true;
}

void searchBrowser() {
    if (browserQuery.empty()) { browserStatus = "Digite uma busca para pesquisar na internet."; dirty = true; return; }
    browserPageView = false;
    browserResults.clear();
    browserLinks.clear();
    browserSelection = 0;
    browserScroll = 0;
    browserResultsOffset = 0;
    browserStatus = "Pesquisando na internet...";
    browserInputFocused = false;
    SDL_StopTextInput();
    startBrowserTask("search", browserQuery);
}

void openBrowserUrl(const std::string& url) {
    if (url.empty()) return;
    browserPageView = true;
    browserPageTitle.clear();
    browserPageText.clear();
    browserLinks.clear();
    browserSelection = 0;
    browserScroll = 0;
    browserResultsOffset = 0;
    browserStatus = "Carregando pagina...";
    startBrowserTask("open", url);
}

void openBrowser() {
    cancelBrowserTask();
    startMenu = false;
    editorOpen = false;
    browserOpen = true;
    browserPageView = false;
    browserInputFocused = true;
    browserQuery.clear();
    queryCaret = 0;
    browserResults.clear();
    browserResultsOffset = 0;
    browserLinks.clear();
    browserStatus = "Pesquise na web; os resultados abrem nesta janela.";
    SDL_StartTextInput();
    dirty = true;
}

void pollChild() {
    if (childPid <= 0) return;
    char buf[512];
    bool changed = false;
    for (;;) {
        ssize_t n = read(childOutput, buf, sizeof(buf));
        if (n > 0) {
            if (childTask == ChildTask::BrowserSearch || childTask == ChildTask::BrowserOpen) {
                for (ssize_t i = 0; i < n; ++i) {
                    if (buf[i] == '\n') {
                        if (!childLineBuffer.empty() && childLineBuffer.back() == '\r') childLineBuffer.pop_back();
                        handleBrowserRecord(childLineBuffer);
                        childLineBuffer.clear();
                    } else if (childLineBuffer.size() < 1600) childLineBuffer.push_back(buf[i]);
                }
            } else if (consoleText.size() < 8000) consoleText.append(buf, static_cast<std::size_t>(n));
            changed = true;
            continue;
        }
        if (n == 0) { close(childOutput); childOutput = -1; break; }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
        close(childOutput); childOutput = -1; break;
    }
    int status = 0;
    pid_t result = waitpid(childPid, &status, WNOHANG);
    if (result == childPid) {
        if (childOutput >= 0) { close(childOutput); childOutput = -1; }
        if (!childLineBuffer.empty() && (childTask == ChildTask::BrowserSearch || childTask == ChildTask::BrowserOpen)) {
            handleBrowserRecord(childLineBuffer);
            childLineBuffer.clear();
        }
        if (browserLoading && (childTask == ChildTask::BrowserSearch || childTask == ChildTask::BrowserOpen)) {
            browserLoading = false;
            if (browserStatus.find("Pesquisando") == 0 || browserStatus.find("Carregando") == 0)
                browserStatus = "A tarefa terminou sem uma resposta. Verifique a rede do console.";
        }
        childPid = -1;
        if (childTask == ChildTask::Program) consoleText += "\n[processo encerrado]";
        childTask = ChildTask::None;
        changed = true;
    }
    if (changed) dirty = true;
}

std::vector<std::vector<std::string>> keyboardRows() {
    return {{"q","w","e","r","t","y","u","i","o","p"},
            {"a","s","d","f","g","h","j","k","l"},
            {"z","x","c","v","b","n","m","Backspace"},
            {"Space","Enter","Fechar"}};
}

int keyboardKeyCount() {
    int count = 0; for (const auto& row : keyboardRows()) count += static_cast<int>(row.size());
    return count;
}

void typeKey(const std::string& key) {
    if (key == "Fechar") { oskOpen = false; dirty = true; return; }
    if (startMenu && menuSearchFocused) {
        if (key == "Backspace") { if (!menuQuery.empty()) menuQuery.pop_back(); menuSelection = 0; }
        else if (key == "Space") { std::size_t pos = menuQuery.size(); appendQueryText(menuQuery, pos, " "); }
        else if (key == "Enter") {
            const auto matches = menuMatches();
            if (!matches.empty()) { oskOpen = false; activate(matches[menuSelection]); return; }
        } else { std::size_t pos = menuQuery.size(); appendQueryText(menuQuery, pos, key.c_str()); menuSelection = 0; }
        dirty = true;
        return;
    }
    if (browserOpen && browserInputFocused) {
        if (key == "Backspace") {
            if (queryCaret > 0 && queryCaret <= browserQuery.size()) { browserQuery.erase(queryCaret - 1, 1); --queryCaret; }
        } else if (key == "Space") {
            appendQueryText(browserQuery, queryCaret, " ");
        } else if (key == "Enter") {
            oskOpen = false; searchBrowser(); return;
        } else appendQueryText(browserQuery, queryCaret, key.c_str());
        dirty = true;
        return;
    }
    if (key == "Backspace") {
        if (caret > 0) { editorText.erase(caret - 1, 1); --caret; }
    } else if (key == "Space") {
        editorText.insert(caret, " "); ++caret;
    } else if (key == "Enter") {
        editorText.insert(caret, "\n"); ++caret;
    } else {
        editorText.insert(caret, key); caret += key.size();
    }
    dirty = true;
}

void navigate(int dx, int dy) {
    if (modalOpen) return;
    if (oskOpen && (editorOpen || browserOpen || startMenu)) {
        int count = keyboardKeyCount();
        oskSelection = (oskSelection + dx + (dy ? dy * 10 : 0) + count * 10) % count;
    } else if (startMenu) {
        const auto matches = menuMatches();
        if (!matches.empty() && dy) menuSelection = (menuSelection + dy + static_cast<int>(matches.size())) % static_cast<int>(matches.size());
    } else if (browserOpen) {
        if (!browserPageView) {
            if (!browserResults.empty() && dy) {
                browserSelection = (browserSelection + dy + static_cast<int>(browserResults.size())) % static_cast<int>(browserResults.size());
                if (browserSelection < browserResultsOffset) browserResultsOffset = browserSelection;
                if (browserSelection >= browserResultsOffset + 5) browserResultsOffset = browserSelection - 4;
            }
        } else if (!browserLinks.empty() && dy) {
            const int count = std::min(3, static_cast<int>(browserLinks.size()));
            browserSelection = (browserSelection + dy + count) % count;
        } else if (dy) browserScroll = std::max(0, browserScroll + dy * 3);
    } else if (editorOpen) {
        if (dx < 0 && caret > 0) --caret;
        if (dx > 0 && caret < editorText.size()) ++caret;
        if (dy < 0) {
            std::size_t line = editorText.rfind('\n', caret == 0 ? 0 : caret - 1);
            caret = line == std::string::npos ? 0 : line;
        }
        if (dy > 0) {
            std::size_t line = editorText.find('\n', caret);
            caret = line == std::string::npos ? editorText.size() : line + 1;
        }
    } else {
        focus = (focus + dx + 4) % 4;
    }
    dirty = true;
}

void activate(int item) {
    startMenu = false;
    menuSearchFocused = false;
    switch (item) {
        case 0: openBrowser(); break;
        case 1: openEditor("hello.py"); break;
        case 2: runAppScript("FileManager"); break;
        case 3: runAppScript("Terminal"); break;
        case 4: openEditor("hello.py"); runCurrentFile(); break;
        default: running = false; break;
    }
    dirty = true;
}

int itemAt(int x, int y) {
    const std::array<SDL_Rect, 3> areas{{SDL_Rect{36, 82, 92, 104}, SDL_Rect{36, 190, 92, 104}, SDL_Rect{36, 298, 92, 104}}};
    const std::array<int, 3> actions{{0, 1, 2}};
    for (int i = 0; i < static_cast<int>(areas.size()); ++i) {
        const auto& r = areas[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return actions[i];
    }
    return -1;
}

void insertText(const char* value) {
    if (!value || !*value) return;
    const std::string s(value);
    editorText.insert(caret, s);
    caret += s.size();
    dirty = true;
}

void editorKey(SDL_Keycode key, SDL_Keymod mod) {
    if (mod & KMOD_CTRL) {
        if (key == SDLK_s) { saveFile(); return; }
        if (key == SDLK_RETURN) { runCurrentFile(); return; }
    }
    if (key == SDLK_ESCAPE) { closeEditor(); return; }
    if (key == SDLK_F5) { runCurrentFile(); return; }
    if (key == SDLK_F1) { oskOpen = !oskOpen; dirty = true; return; }
    if (key == SDLK_LEFT && caret > 0) --caret;
    else if (key == SDLK_RIGHT && caret < editorText.size()) ++caret;
    else if (key == SDLK_HOME) {
        std::size_t line = editorText.rfind('\n', caret == 0 ? 0 : caret - 1);
        caret = line == std::string::npos ? 0 : line + 1;
    } else if (key == SDLK_END) {
        std::size_t line = editorText.find('\n', caret);
        caret = line == std::string::npos ? editorText.size() : line;
    } else if (key == SDLK_BACKSPACE && caret > 0) { editorText.erase(caret - 1, 1); --caret; }
    else if (key == SDLK_DELETE && caret < editorText.size()) editorText.erase(caret, 1);
    else if (key == SDLK_RETURN) { editorText.insert(caret, "\n"); ++caret; }
    else if (key == SDLK_TAB) { editorText.insert(caret, "    "); caret += 4; }
    dirty = true;
}

void clickOsk(int x, int y) {
    const auto rows = keyboardRows();
    int index = 0;
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const int count = static_cast<int>(rows[row].size());
        const int keyW = row == 3 ? 190 : 72;
        const int gap = 8;
        const int total = count * keyW + (count - 1) * gap;
        const int startX = (W - total) / 2;
        const int yy = 493 + row * 36;
        for (int col = 0; col < count; ++col, ++index) {
            SDL_Rect r{startX + col * (keyW + gap), yy, keyW, 30};
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) { typeKey(rows[row][col]); return; }
        }
    }
}

void createNewFile() {
    std::string name = "novo.py";
    int suffix = 2;
    while (access(filePath(name).c_str(), F_OK) == 0 && suffix < 100) name = "novo" + std::to_string(suffix++) + ".py";
    activeName = name;
    editorText.clear();
    caret = 0;
    consoleText = "Novo arquivo. Edite e use Salvar para gravar no Workspace.";
    dirty = true;
}

void placeCaretFromClick(int x, int y) {
    if (y < 145 || y >= 480 || x < 326) return;
    const int targetLine = (y - 145) / 23;
    std::size_t pos = 0;
    for (int row = 0; row < targetLine; ++row) {
        const std::size_t end = editorText.find('\n', pos);
        if (end == std::string::npos) { pos = editorText.size(); break; }
        pos = end + 1;
    }
    const std::size_t end = editorText.find('\n', pos);
    const std::size_t lineEnd = end == std::string::npos ? editorText.size() : end;
    const int targetX = x - 326;
    int width = 0;
    caret = pos;
    while (caret < lineEnd) {
        const int nextWidth = textWidth(editorText.substr(pos, caret - pos + 1), 16);
        if (nextWidth > targetX) break;
        width = nextWidth;
        ++caret;
    }
    if (targetX - width > 8 && caret < lineEnd) ++caret;
    dirty = true;
}

void click(int x, int y) {
    if (modalOpen) { modalOpen = false; dirty = true; return; }
    if (oskOpen && (editorOpen || browserOpen || startMenu)) { clickOsk(x, y); return; }
    if (browserOpen) {
        if (x >= 1050 && x < 1204 && y >= 47 && y < 100) { searchBrowser(); return; }
        if (x >= 196 && x < 1038 && y >= 47 && y < 100) {
            browserInputFocused = true; queryCaret = browserQuery.size(); SDL_StartTextInput(); dirty = true; return;
        }
        if (x >= 33 && x < 166 && y >= 47 && y < 100) {
            if (browserPageView) { browserPageView = false; browserStatus = "Resultados da pesquisa"; dirty = true; }
            else { browserOpen = false; SDL_StopTextInput(); dirty = true; }
            return;
        }
        if (x >= 1110 && x < 1205 && y >= 108 && y < 151) { oskOpen = !oskOpen; browserInputFocused = true; queryCaret = browserQuery.size(); SDL_StartTextInput(); dirty = true; return; }
        if (!browserPageView && y >= 188) {
            const int row = (y - 188) / 88;
            const int index = browserResultsOffset + row;
            const int rowY = 188 + row * 88;
            if (x >= 96 && x < 1208 && row >= 0 && row < 5 && y < rowY + 78 && index < static_cast<int>(browserResults.size())) {
                browserSelection = index; openBrowserUrl(browserResults[index].url); return;
            }
        } else if (y >= 550 && y < 631) {
            const int row = (y - 550) / 26;
            if (row >= 0 && row < static_cast<int>(browserLinks.size())) { browserSelection = row; openBrowserUrl(browserLinks[row].url); return; }
        }
        browserInputFocused = false;
        SDL_StopTextInput();
        return;
    }
    if (editorOpen) {
        if (x >= 950 && x < 1062 && y >= 91 && y < 142) { saveFile(); return; }
        if (x >= 1070 && x < 1198 && y >= 91 && y < 142) { runCurrentFile(); return; }
        if (x >= 825 && x < 938 && y >= 91 && y < 142) { oskOpen = !oskOpen; dirty = true; return; }
        if (x >= 698 && x < 811 && y >= 91 && y < 142) { createNewFile(); return; }
        if (x >= 1108 && x < 1203 && y >= 32 && y < 68) { closeEditor(); return; }
        if (x >= 61 && x < 256 && y >= 157 && y < 579) {
            const auto files = workspaceFiles();
            const int row = (y - 157) / 38;
            if (row >= 0 && row < static_cast<int>(files.size())) { loadFile(files[row]); return; }
        }
        if (x >= 267 && x < 1215 && y >= 145 && y < 480) { placeCaretFromClick(x, y); return; }
        return;
    }
    if (startMenu) {
        if (x >= 47 && x < 422 && y >= 220 && y < 268) {
            menuSearchFocused = true; menuSelection = 0; SDL_StartTextInput(); dirty = true; return;
        }
        if (x >= 41 && x < 434 && y >= 279 && y < 608) {
            const int row = (y - 280) / 54;
            const auto matches = menuMatches();
            if (row >= 0 && row < static_cast<int>(matches.size())) activate(matches[row]);
        } else {
            startMenu = false; menuSearchFocused = false; SDL_StopTextInput(); dirty = true;
        }
        return;
    }
    if (y >= 648) {
        if (x >= 20 && x < 78) { startMenu = !startMenu; menuQuery.clear(); menuSelection = 0; menuSearchFocused = false; if (startMenu) SDL_StartTextInput(); else SDL_StopTextInput(); dirty = true; return; }
        if (x >= 180 && x < 228) { openBrowser(); return; }
        if (x >= 232 && x < 280) { runAppScript("FileManager"); return; }
        if (x >= 284 && x < 332) { openEditor("hello.py"); return; }
        if (x >= 336 && x < 384) { runAppScript("Terminal"); return; }
    }
    const int item = itemAt(x, y);
    if (item >= 0) { activate(item); return; }
}

void drawBackground() {
    color(ink); SDL_RenderClear(renderer);
    for (int y = 0; y < H; y += 6) {
        const float t = static_cast<float>(y) / static_cast<float>(H);
        const Uint8 r = static_cast<Uint8>(16 + 19 * t);
        const Uint8 g = static_cast<Uint8>(23 + 17 * t);
        const Uint8 b = static_cast<Uint8>(49 + 27 * t);
        fill(0, y, W, 7, Color{r, g, b, 255});
    }
    // Abstract light bands inspired by Zorin layouts, drawn locally.
    for (int i = 0; i < 34; ++i) {
        const int x = 720 + i * 18;
        const Uint8 alpha = static_cast<Uint8>(std::max(0, 18 - i / 2));
        fill(x, 88 + i * 2, 10, 420 - i * 5, Color{72, 121, 211, alpha});
    }
    for (int i = 0; i < 12; ++i) {
        const int x = 790 + i * 22;
        fill(x, 584 - i * 7, 16, 2, Color{102, 151, 218, static_cast<Uint8>(35 - i)});
    }
}

void drawLauncherIcon(int x, int y, const char* glyph, Color accent, bool selected = false) {
    fill(x, y, 42, 42, selected ? Color{49, 68, 104, 255} : Color{29, 40, 63, 255});
    outline(x, y, 42, 42, selected ? blue : Color{60, 77, 109, 255});
    fill(x + 5, y + 5, 32, 32, accent);
    drawText(glyph, x + 9, y + 11, 17, white);
}

void drawSystemStatus() {
    int x = 772;
    if (consolePlatform && deviceStatus.hasMemory) {
        const auto used = deviceStatus.memoryTotalBytes - std::min(deviceStatus.memoryTotalBytes, deviceStatus.memoryAvailableBytes);
        drawText("RAM " + gibibytes(used) + "/" + gibibytes(deviceStatus.memoryTotalBytes), x, 674, 12, muted);
        x += 128;
    }
    if (consolePlatform && deviceStatus.hasStorage) {
        drawText("SD " + gibibytes(deviceStatus.storageAvailableBytes, 0), x, 674, 12, muted);
        x += 74;
    }
    if (consolePlatform && deviceStatus.hasBattery) {
        drawText("BAT " + std::to_string(deviceStatus.batteryPercent) + "%", x, 674, 12,
                 deviceStatus.batteryPercent <= 15 ? Color{255, 132, 133, 255} : muted);
        x += 76;
    }
    if (!consolePlatform) drawText("PREVIA PC", x, 674, 12, muted);
    drawText(clockText(), 1120, 673, 13, white);
}

void drawHome() {
    drawBackground();
    fill(0, 0, W, 43, Color{9, 13, 26, 160});
    fill(28, 10, 24, 24, blue);
    drawText("Z", 34, 12, 18, white);
    drawText("CROSSMIX  /  DESKTOP DE DESENVOLVIMENTO", 64, 14, 14, white);
    drawText("WORKSPACE LOCAL", 1051, 15, 12, muted);

    if (!startMenu) {
        drawText("DESKTOP", 148, 112, 13, Color{190, 208, 242, 255});
        fill(148, 139, 42, 2, blue);
        drawText("Crie. Teste. Continue.", 148, 162, 31, white);
        drawText("Seu workspace Python e JavaScript fica no cartao SD.", 150, 210, 17, muted);
        const auto files = workspaceFiles();
        drawText(std::to_string(files.size()) + " arquivos no Workspace", 150, 248, 14, Color{176, 197, 230, 255});
        drawText("START abre o menu   |   analogico direito move o ponteiro", 150, 286, 14, muted);
    }

    fill(0, 648, W, 72, Color{8, 12, 24, 235});
    fill(0, 648, W, 1, Color{100, 120, 158, 155});
    fill(21, 659, 48, 48, startMenu ? Color{83, 128, 228, 255} : Color{41, 56, 87, 255});
    outline(21, 659, 48, 48, Color{106, 137, 195, 255});
    drawText("Z", 37, 670, 27, white);
    const std::array<const char*, 4> glyphs{{"W", "F", "</>", ">_"}};
    const std::array<Color, 4> accents{{Color{73, 137, 220, 255}, Color{214, 157, 79, 255}, Color{96, 111, 231, 255}, Color{83, 184, 148, 255}}};
    const std::array<int, 4> xs{{184, 238, 292, 346}};
    for (int i = 0; i < 4; ++i) {
        drawLauncherIcon(xs[i], 656, glyphs[i], accents[i], focus == i);
        if (focus == i) fill(xs[i] + 11, 705, 20, 2, blue);
    }
    drawSystemStatus();

    if (startMenu) {
        fill(22, 173, 434, 462, Color{15, 22, 39, 249});
        outline(22, 173, 434, 462, Color{76, 99, 145, 255});
        drawText("Iniciar", 46, 188, 23, white);
        drawText("Aplicativos", 323, 196, 12, muted);
        fill(43, 220, 392, 48, menuSearchFocused ? Color{46, 63, 97, 255} : Color{29, 39, 59, 255});
        outline(43, 220, 392, 48, menuSearchFocused ? blue : Color{66, 79, 108, 255});
        drawText(menuQuery.empty() ? "Pesquisar aplicativos" : menuQuery.substr(0, 37), 58, 234, 16, menuQuery.empty() ? muted : white);
        const auto matches = menuMatches();
        for (int row = 0; row < static_cast<int>(matches.size()) && row < 6; ++row) {
            const auto& item = menuItems()[matches[row]];
            const int y = 279 + row * 56;
            if (menuSelection == row) fill(39, y, 400, 50, Color{42, 60, 93, 255});
            fill(51, y + 8, 34, 34, Color{47, 67, 102, 255});
            drawText(item.glyph, 56, y + 16, 14, blue);
            drawText(item.label, 97, y + 7, 16, white);
            drawText(item.detail, 97, y + 29, 12, muted);
        }
        if (matches.empty()) drawText("Nenhum aplicativo encontrado.", 58, 296, 15, muted);
    }
    if (startMenu && oskOpen) drawOsk();
}

void drawOsk() {
    fill(286, 480, 922, 160, Color{13, 20, 34, 250});
    outline(286, 480, 922, 160, Color{66, 88, 126, 255});
    const auto rows = keyboardRows();
    int index = 0;
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const int count = static_cast<int>(rows[row].size());
        const int keyW = row == 3 ? 190 : 72;
        const int gap = 8;
        const int total = count * keyW + (count - 1) * gap;
        const int startX = (W - total) / 2;
        const int y = 493 + row * 36;
        for (int col = 0; col < count; ++col, ++index) {
            const int x = startX + col * (keyW + gap);
            fill(x, y, keyW, 30, index == oskSelection ? blue : Color{39, 52, 74, 255});
            drawText(rows[row][col], x + 10, y + 6, 14, white);
        }
    }
}

void drawEditor() {
    drawBackground();
    fill(34, 28, W - 68, 600, Color{15, 20, 31, 248});
    outline(34, 28, W - 68, 600, Color{67, 82, 111, 255});
    fill(34, 28, W - 68, 42, Color{29, 37, 53, 255});
    drawText("Code", 52, 39, 18, white);
    drawText("CrossMix Desktop  /  " + activeName, 129, 40, 14, muted);
    fill(1108, 35, 95, 28, Color{43, 67, 103, 255}); drawText("Desktop", 1120, 41, 14, white);

    fill(49, 83, 218, 533, Color{19, 27, 41, 255});
    drawText("EXPLORER", 66, 100, 13, muted);
    drawText("WORKSPACE", 66, 130, 12, Color{135, 157, 191, 255});
    const auto files = workspaceFiles();
    for (int i = 0; i < static_cast<int>(files.size()) && i < 10; ++i) {
        const int fy = 151 + i * 38;
        if (files[i] == activeName) fill(59, fy, 197, 35, Color{39, 57, 87, 255});
        const bool pythonFile = files[i].size() >= 3 && files[i].substr(files[i].size() - 3) == ".py";
        const bool jsFile = files[i].size() >= 3 && files[i].substr(files[i].size() - 3) == ".js";
        drawText(pythonFile ? "Py" : (jsFile ? "JS" : "T"), 66, fy + 9, 12, pythonFile ? Color{103, 183, 250, 255} : violet);
        drawText(files[i].substr(0, 19), 95, fy + 8, 13, white);
    }
    drawText("/Data/Desktop/Workspace", 63, 581, 10, muted);

    fill(267, 83, 948, 533, Color{16, 22, 34, 255});
    fill(267, 83, 948, 42, Color{23, 31, 47, 255});
    fill(278, 86, 160, 38, Color{16, 22, 34, 255});
    drawText(activeName, 292, 96, 16, white);
    fill(698, 91, 113, 32, Color{41, 57, 82, 255}); drawText("Novo arquivo", 710, 100, 13, white);
    fill(825, 91, 113, 32, Color{41, 57, 82, 255}); drawText("Teclado", 846, 100, 13, white);
    fill(950, 91, 112, 32, Color{41, 57, 82, 255}); drawText("Salvar", 983, 100, 14, white);
    fill(1070, 91, 128, 32, Color{38, 106, 94, 255}); drawText("Executar", 1101, 100, 14, white);

    if (!oskOpen) {
        int y = 145, lineNo = 1;
        std::size_t pos = 0;
        int cursorLine = 0, cursorCol = 0;
        for (std::size_t i = 0; i < caret && i < editorText.size(); ++i) {
            if (editorText[i] == '\n') { ++cursorLine; cursorCol = 0; } else ++cursorCol;
        }
        while (pos <= editorText.size() && y < 455) {
            const std::size_t end = editorText.find('\n', pos);
            const std::string line = editorText.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            drawText(std::to_string(lineNo), 285, y, 14, Color{101, 117, 143, 255});
            Color code = (line.find("print") != std::string::npos || line.find("console") != std::string::npos || line.find("import") != std::string::npos)
                ? Color{126, 190, 255, 255} : Color{214, 222, 237, 255};
            drawText(line.substr(0, 110), 326, y, 15, code);
            if (lineNo - 1 == cursorLine) {
                const std::string prefix = line.substr(0, std::min<std::size_t>(cursorCol, line.size()));
                const int cx = 326 + textWidth(prefix, 15);
                fill(cx, y + 1, 2, 18, blue);
            }
            if (end == std::string::npos) break;
            pos = end + 1; y += 23; ++lineNo;
        }
        fill(267, 490, 948, 126, Color{13, 19, 30, 255});
        drawText("TERMINAL DE SAIDA", 286, 500, 12, muted);
        if (consoleText.empty()) drawText("F5 executa Python/JavaScript. A saida do processo aparece aqui.", 286, 523, 14, muted);
        else {
            std::vector<std::string> lines;
            std::size_t start = 0;
            while (start <= consoleText.size()) {
                std::size_t end = consoleText.find('\n', start);
                lines.push_back(consoleText.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
            const int begin = std::max(0, static_cast<int>(lines.size()) - 4);
            int oy = 520;
            for (int i = begin; i < static_cast<int>(lines.size()); ++i, oy += 20)
                drawText(lines[i].substr(0, 130), 286, oy, 13, childPid > 0 ? green : muted);
        }
    } else drawOsk();

    fill(0, 640, W, 80, Color{9, 13, 25, 245});
    drawText("CrossMix Code", 38, 664, 15, white);
    drawText("SELECT: teclado    X: executar    Y: salvar    B: desktop", 376, 664, 13, muted);
    drawSystemStatus();
}

std::vector<std::string> wrapLines(const std::string& text, std::size_t width) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string source = text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
        std::size_t pos = 0;
        while (pos < source.size()) {
            std::size_t end = std::min(source.size(), pos + width);
            if (end < source.size()) {
                const std::size_t space = source.rfind(' ', end);
                if (space != std::string::npos && space > pos) end = space;
            }
            result.push_back(source.substr(pos, end - pos));
            pos = end;
            while (pos < source.size() && source[pos] == ' ') ++pos;
        }
        if (source.empty()) result.emplace_back();
        if (newline == std::string::npos) break;
        start = newline + 1;
    }
    return result;
}

void drawBrowser() {
    drawBackground();
    fill(0, 0, W, 39, Color{10, 15, 27, 238});
    drawText("Internet", 36, 10, 18, white);
    drawText("Navegador leve", 126, 13, 13, muted);
    fill(31, 47, 143, 48, Color{35, 49, 74, 255});
    outline(31, 47, 143, 48, Color{70, 88, 124, 255});
    drawText(browserPageView ? "← Resultados" : "← Desktop", 45, 62, 15, white);
    fill(188, 47, 852, 48, browserInputFocused ? Color{43, 58, 88, 255} : Color{25, 35, 54, 255});
    outline(188, 47, 852, 48, browserInputFocused ? blue : Color{65, 79, 106, 255});
    drawText(browserQuery.empty() ? "Pesquisar na web" : browserQuery.substr(0, 91), 205, 62, 16, browserQuery.empty() ? muted : white);
    fill(1048, 47, 156, 48, Color{59, 104, 177, 255});
    drawText("Pesquisar", 1083, 62, 16, white);
    fill(1110, 108, 94, 40, Color{35, 49, 74, 255});
    outline(1110, 108, 94, 40, Color{70, 88, 124, 255});
    drawText("Teclado", 1124, 120, 13, white);
    drawText(browserStatus.substr(0, 126), 42, 122, 14, browserLoading ? blue : muted);

    if (browserPageView) {
        fill(31, 164, 1214, 472, Color{17, 24, 38, 245});
        outline(31, 164, 1214, 472, Color{55, 72, 102, 255});
        drawText(browserPageTitle.empty() ? "Pagina web" : browserPageTitle.substr(0, 82), 55, 179, 20, white);
        drawText("Conteudo de texto • links publicos HTTP/HTTPS", 56, 211, 12, muted);
        const auto lines = wrapLines(browserPageText, 138);
        int y = 239;
        const int start = std::min(browserScroll, std::max(0, static_cast<int>(lines.size()) - 1));
        for (int i = start; i < static_cast<int>(lines.size()) && y < 523; ++i, y += 20)
            drawText(lines[i], 56, y, 14, Color{215, 224, 240, 255});
        if (browserLoading && browserPageText.empty()) drawText("Conectando...", 56, 260, 16, muted);
        if (browserLinks.empty()) drawText("B volta aos resultados   |   analógico/direcional rola o texto", 56, 568, 13, muted);
        for (int i = 0; i < static_cast<int>(browserLinks.size()) && i < 3; ++i) {
            const int yLink = 550 + i * 26;
            if (browserSelection == i) fill(48, yLink - 3, 1168, 24, Color{39, 57, 88, 255});
            drawText("↗ " + browserLinks[i].label.substr(0, 126), 57, yLink, 13, blue);
        }
    } else {
        drawText("Resultados", 42, 164, 16, white);
        if (browserResults.empty() && !browserLoading) {
            drawText("Digite uma pesquisa e pressione A ou clique em Pesquisar.", 42, 208, 17, muted);
            drawText("A busca usa HTTPS; paginas aparecem como texto e links nesta janela.", 42, 238, 14, muted);
        }
        for (int row = 0; row < 5 && browserResultsOffset + row < static_cast<int>(browserResults.size()); ++row) {
            const int i = browserResultsOffset + row;
            const int y = 188 + row * 88;
            fill(35, y, 1208, 78, browserSelection == i ? Color{34, 49, 77, 247} : Color{18, 27, 44, 235});
            outline(35, y, 1208, 78, browserSelection == i ? blue : Color{51, 67, 96, 255});
            drawText(browserResults[i].title.substr(0, 116), 56, y + 10, 17, white);
            drawText(browserResults[i].snippet.substr(0, 144), 56, y + 36, 13, muted);
            drawText(browserResults[i].url.substr(0, 150), 56, y + 58, 11, blue);
        }
    }

    fill(0, 648, W, 72, Color{8, 12, 24, 235});
    fill(0, 648, W, 1, Color{100, 120, 158, 155});
    drawText("A: abrir   B: voltar   SELECT: teclado   analógico direito: ponteiro", 35, 674, 14, muted);
    drawSystemStatus();
    if (oskOpen) drawOsk();
}

void drawPointer() {
    const SDL_Point outer[]{{cursorX, cursorY}, {cursorX, cursorY + 20}, {cursorX + 5, cursorY + 15},
                            {cursorX + 9, cursorY + 23}, {cursorX + 13, cursorY + 21}, {cursorX + 9, cursorY + 13},
                            {cursorX + 16, cursorY + 13}, {cursorX, cursorY}};
    color(Color{5, 10, 20, 255});
    SDL_RenderDrawLines(renderer, outer, static_cast<int>(sizeof(outer) / sizeof(outer[0])));
    const SDL_Point inner[]{{cursorX + 2, cursorY + 4}, {cursorX + 2, cursorY + 16}, {cursorX + 6, cursorY + 12},
                            {cursorX + 10, cursorY + 20}, {cursorX + 11, cursorY + 19}, {cursorX + 7, cursorY + 11},
                            {cursorX + 13, cursorY + 11}, {cursorX + 2, cursorY + 4}};
    color(white);
    SDL_RenderDrawLines(renderer, inner, static_cast<int>(sizeof(inner) / sizeof(inner[0])));
}

void drawModal() {
    fill(0, 0, W, H, Color{0, 0, 0, 155});
    fill(388, 252, 504, 214, Color{22, 31, 50, 255});
    outline(388, 252, 504, 214, Color{93, 122, 175, 255});
    drawText(modalTitle, 420, 280, 24, white);
    std::size_t start = 0; int y = 328;
    while (start <= modalBody.size() && y < 419) {
        std::size_t end = modalBody.find('\n', start);
        drawText(modalBody.substr(start, end == std::string::npos ? std::string::npos : end - start), 420, y, 16, muted);
        if (end == std::string::npos) break;
        start = end + 1; y += 25;
    }
    drawText("A / clique: fechar", 420, 430, 14, blue);
}

void render() {
    if (editorOpen) drawEditor();
    else if (browserOpen) drawBrowser();
    else drawHome();
    if (modalOpen) drawModal();
    drawPointer();
    if (!capturePath.empty() && !captured && !(browserOpen && browserLoading)) {
        SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
        if (shot) {
            if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0)
                SDL_SaveBMP(shot, capturePath.c_str());
            SDL_FreeSurface(shot);
        }
        captured = true;
        running = false;
    }
    SDL_RenderPresent(renderer);
    dirty = false;
}

void doBack() {
    if (modalOpen) modalOpen = false;
    else if (oskOpen) oskOpen = false;
    else if (startMenu) { startMenu = false; menuSearchFocused = false; SDL_StopTextInput(); }
    else if (browserOpen && browserPageView) { browserPageView = false; browserStatus = "Resultados da pesquisa"; browserSelection = 0; }
    else if (browserOpen) { cancelBrowserTask(); browserOpen = false; browserInputFocused = false; SDL_StopTextInput(); }
    else if (editorOpen) closeEditor();
    else running = false;
    dirty = true;
}

void activateFocused() {
    if (modalOpen) { modalOpen = false; dirty = true; return; }
    if (usePointerForConfirm) { click(cursorX, cursorY); return; }
    if ((editorOpen || browserOpen || startMenu) && oskOpen) {
        const auto rows = keyboardRows();
        int index = oskSelection;
        for (const auto& row : rows) {
            if (index < static_cast<int>(row.size())) { typeKey(row[index]); return; }
            index -= static_cast<int>(row.size());
        }
        return;
    }
    if (startMenu) {
        const auto matches = menuMatches();
        if (!matches.empty() && menuSelection >= 0 && menuSelection < static_cast<int>(matches.size())) activate(matches[menuSelection]);
        return;
    }
    if (browserOpen) {
        if (browserInputFocused) { searchBrowser(); return; }
        if (browserPageView) {
            const int link = std::min(browserSelection, std::min(2, static_cast<int>(browserLinks.size()) - 1));
            if (link >= 0 && link < static_cast<int>(browserLinks.size())) openBrowserUrl(browserLinks[link].url);
        } else if (browserSelection >= 0 && browserSelection < static_cast<int>(browserResults.size()))
            openBrowserUrl(browserResults[browserSelection].url);
        return;
    }
    if (editorOpen) { return; }
    const std::array<int, 4> actionForFocus{{0, 2, 1, 3}};
    activate(actionForFocus[std::clamp(focus, 0, 3)]);
}

void keyDown(SDL_Keycode key, SDL_Keymod mod) {
    if (modalOpen) { modalOpen = false; dirty = true; return; }
    if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT) usePointerForConfirm = false;
    if (browserOpen) {
        if (oskOpen && (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT)) {
            navigate(key == SDLK_LEFT ? -1 : key == SDLK_RIGHT ? 1 : 0, key == SDLK_UP ? -1 : key == SDLK_DOWN ? 1 : 0);
            return;
        }
        if (key == SDLK_ESCAPE) { doBack(); return; }
        if (key == SDLK_F1) { oskOpen = !oskOpen; browserInputFocused = !browserPageView; if (oskOpen) SDL_StartTextInput(); else SDL_StopTextInput(); dirty = true; return; }
        if (key == SDLK_BACKSPACE && browserInputFocused) {
            if (queryCaret > 0 && queryCaret <= browserQuery.size()) { browserQuery.erase(queryCaret - 1, 1); --queryCaret; dirty = true; }
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { activateFocused(); return; }
        if (key == SDLK_UP || key == SDLK_DOWN) {
            if (browserInputFocused) browserInputFocused = false;
            navigate(0, key == SDLK_UP ? -1 : 1); return;
        }
        if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) { browserScroll = std::max(0, browserScroll + (key == SDLK_PAGEUP ? -12 : 12)); dirty = true; return; }
        return;
    }
    if (editorOpen) {
        if (oskOpen && (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT)) {
            navigate(key == SDLK_LEFT ? -1 : key == SDLK_RIGHT ? 1 : 0, key == SDLK_UP ? -1 : key == SDLK_DOWN ? 1 : 0);
            return;
        }
        editorKey(key, mod);
        return;
    }
    if (key == SDLK_F1 || key == SDLK_HOME || key == SDLK_LGUI || key == SDLK_RGUI) {
        startMenu = !startMenu; menuSearchFocused = false; if (startMenu) SDL_StartTextInput(); else SDL_StopTextInput(); dirty = true; return;
    }
    if (startMenu) {
        const auto matches = menuMatches();
        if (key == SDLK_BACKSPACE && menuSearchFocused) {
            if (!menuQuery.empty()) menuQuery.pop_back();
            menuSelection = 0;
        }
        else if (key == SDLK_UP && !matches.empty()) { menuSearchFocused = false; menuSelection = (menuSelection + static_cast<int>(matches.size()) - 1) % static_cast<int>(matches.size()); }
        else if (key == SDLK_DOWN && !matches.empty()) { menuSearchFocused = false; menuSelection = (menuSelection + 1) % static_cast<int>(matches.size()); }
        else if (key == SDLK_RETURN || key == SDLK_SPACE) {
            if (menuSearchFocused) menuSearchFocused = false;
            else if (!matches.empty()) activate(matches[menuSelection]);
        }
        else if (key == SDLK_ESCAPE) { startMenu = false; menuSearchFocused = false; SDL_StopTextInput(); }
        dirty = true; return;
    }
    if (key == SDLK_LEFT) navigate(-1, 0);
    else if (key == SDLK_RIGHT) navigate(1, 0);
    else if (key == SDLK_RETURN || key == SDLK_SPACE) activateFocused();
    else if (key == SDLK_ESCAPE) doBack();
    else if (key == SDLK_TAB) { focus = (focus + 1) % 4; dirty = true; }
}

void hat(int x, int y) {
    if (!x && !y) return;
    usePointerForConfirm = false;
    const unsigned int now = SDL_GetTicks();
    if (now - lastPadRepeat < 115) return;
    lastPadRepeat = now;
    navigate(x, y);
}

void event(const SDL_Event& e) {
    if (e.type == SDL_QUIT) { running = false; return; }
    if (e.type == SDL_WINDOWEVENT) { dirty = true; return; }
    if (e.type == SDL_MOUSEMOTION) { cursorX = std::clamp(e.motion.x, 0, W - 1); cursorY = std::clamp(e.motion.y, 0, H - 1); usePointerForConfirm = true; dirty = true; }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) { cursorX = e.button.x; cursorY = e.button.y; usePointerForConfirm = true; click(e.button.x, e.button.y); }
    if (e.type == SDL_MOUSEWHEEL && (editorOpen || browserOpen)) { navigate(0, e.wheel.y > 0 ? -1 : 1); }
    if (e.type == SDL_TEXTINPUT) {
        if (editorOpen) insertText(e.text.text);
        else if (browserOpen && browserInputFocused) appendQueryText(browserQuery, queryCaret, e.text.text);
        else if (startMenu && menuSearchFocused) { std::size_t caretPos = menuQuery.size(); appendQueryText(menuQuery, caretPos, e.text.text); menuSelection = 0; }
    }
    if (e.type == SDL_KEYDOWN && e.key.repeat == 0) keyDown(e.key.keysym.sym, static_cast<SDL_Keymod>(e.key.keysym.mod));
    if (e.type == SDL_JOYHATMOTION) {
        int x = (e.jhat.value & SDL_HAT_LEFT) ? -1 : (e.jhat.value & SDL_HAT_RIGHT) ? 1 : 0;
        int y = (e.jhat.value & SDL_HAT_UP) ? -1 : (e.jhat.value & SDL_HAT_DOWN) ? 1 : 0;
        hat(x, y);
    }
    if (e.type == SDL_JOYAXISMOTION) {
        if (e.jaxis.axis == 0) leftAxisX = e.jaxis.value;
        if (e.jaxis.axis == 1) leftAxisY = e.jaxis.value;
        rightStick.handleAxisEvent(e);
    }
    if (e.type == SDL_JOYBUTTONDOWN) {
        if (e.jbutton.button == 0) activateFocused();
        else if (e.jbutton.button == 1) doBack();
        else if (e.jbutton.button == 2 && editorOpen) saveFile();
        else if (e.jbutton.button == 3 && editorOpen) runCurrentFile();
        else if (e.jbutton.button == 6 && (editorOpen || browserOpen || startMenu)) {
            oskOpen = !oskOpen;
            if (browserOpen) { browserInputFocused = oskOpen && !browserPageView; if (browserInputFocused) SDL_StartTextInput(); else SDL_StopTextInput(); }
            if (startMenu) { menuSearchFocused = oskOpen; if (menuSearchFocused) SDL_StartTextInput(); else SDL_StopTextInput(); }
            dirty = true;
        }
        else if (e.jbutton.button == 7) {
            if (editorOpen) closeEditor();
            else if (browserOpen) doBack();
            else { startMenu = !startMenu; menuSearchFocused = false; if (startMenu) SDL_StartTextInput(); else SDL_StopTextInput(); dirty = true; }
        }
        else if (e.jbutton.button == 4) hat(-1, 0);
        else if (e.jbutton.button == 5) hat(1, 0);
    }
}

} // namespace

int main() {
    rootDir = env("CROSSMIX_DESKTOP_ROOT", "/mnt/SDCARD");
    appDir = env("CROSSMIX_DESKTOP_APP", "/mnt/SDCARD/Apps/Desktop");
    dataDir = env("CROSSMIX_DESKTOP_DATA", "/mnt/SDCARD/Data/Desktop");
    workspaceDir = path(dataDir, "Workspace");
    capturePath = env("CROSSMIX_DESKTOP_CAPTURE", "");
    consolePlatform = env("CROSSMIX_DESKTOP_PLATFORM", "console") != "preview";
    if (!consolePlatform && env("CROSSMIX_DESKTOP_START_MENU", "0") == "1") startMenu = true;
    mkdir(dataDir.c_str(), 0775);
    mkdir(workspaceDir.c_str(), 0775);
    seedWorkspace();

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1;
    }
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "SDL_ttf: %s\n", TTF_GetError()); SDL_Quit(); return 1;
    }
    const Uint32 flags = consolePlatform ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    window = SDL_CreateWindow("CrossMix Desktop", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, flags);
    if (!window) { std::fprintf(stderr, "Janela: %s\n", SDL_GetError()); TTF_Quit(); SDL_Quit(); return 1; }
    renderer = SDL_CreateRenderer(window, -1, consolePlatform ? SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC : SDL_RENDERER_SOFTWARE);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) { std::fprintf(stderr, "Renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1; }
    SDL_RenderSetLogicalSize(renderer, W, H);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_ShowCursor(SDL_DISABLE);
    if (startMenu) SDL_StartTextInput();
    SDL_JoystickEventState(SDL_ENABLE);
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        SDL_Joystick* joystick = SDL_JoystickOpen(i);
        if (joystick) joysticks.push_back(joystick);
    }
    if (!font(18)) {
        std::fprintf(stderr, "Fonte nao encontrada em %s/font.ttf\n", appDir.c_str());
        for (SDL_Joystick* j : joysticks) SDL_JoystickClose(j);
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1;
    }
    const std::string previewQuery = env("CROSSMIX_DESKTOP_START_BROWSER_QUERY", "");
    if (!consolePlatform && !previewQuery.empty()) {
        openBrowser();
        browserQuery = previewQuery;
        queryCaret = browserQuery.size();
        searchBrowser();
    }

    while (running) {
        const uint64_t now = SDL_GetTicks64();
        if (rightStick.update(now, cursorX, cursorY)) {
            usePointerForConfirm = true;
            SDL_WarpMouseInWindow(window, cursorX, cursorY);
            dirty = true;
        }
        if ((std::abs(leftAxisX) > 14500 || std::abs(leftAxisY) > 14500) && now - lastAnalogNav > 185) {
            lastAnalogNav = now;
            hat(std::abs(leftAxisX) > 14500 ? (leftAxisX < 0 ? -1 : 1) : 0,
                std::abs(leftAxisY) > 14500 ? (leftAxisY < 0 ? -1 : 1) : 0);
        }
        pollChild();
        if (consolePlatform && statusReader.refresh(rootDir, now)) deviceStatus = statusReader.snapshot();
        if (now - lastClockDraw > 1000) { lastClockDraw = now; dirty = true; }
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 20)) { event(e); while (SDL_PollEvent(&e)) event(e); }
        if (dirty) render();
    }

    if (childPid > 0) { kill(childPid, SIGTERM); waitpid(childPid, nullptr, 0); }
    if (childOutput >= 0) close(childOutput);
    for (SDL_Joystick* j : joysticks) SDL_JoystickClose(j);
    for (TTF_Font* f : fonts) if (f) TTF_CloseFont(f);
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    TTF_Quit(); SDL_Quit();
    return 0;
}
