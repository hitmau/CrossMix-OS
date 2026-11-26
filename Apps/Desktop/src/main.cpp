#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
const Color panel{20, 30, 51, 238};
const Color panelLight{34, 48, 72, 242};
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
int menuSelection = 0, focus = 0, oskSelection = 0;
int cursorX = W / 2, cursorY = H / 2;
int axisX = 0, axisY = 0;
pid_t childPid = -1;
int childOutput = -1;
uint64_t lastClockDraw = 0, lastPointerTick = 0;
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

void card(int x, int y, int w, int h, Color accent, const std::string& glyph,
          const std::string& title, const std::string& subtitle, bool selected = false) {
    fill(x, y, w, h, selected ? panelLight : panel);
    outline(x, y, w, h, selected ? blue : Color{53, 70, 98, 255});
    fill(x + 18, y + 18, 54, 54, accent);
    drawText(glyph, x + 33, y + 28, 30, white);
    drawText(title, x + 18, y + 90, 21, white);
    drawText(subtitle, x + 18, y + 124, 15, muted);
}

std::string clockText() {
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    char buf[64]{};
    if (local) std::strftime(buf, sizeof(buf), "%H:%M  %d/%m", local);
    return buf;
}

std::string filePath(const std::string& name) { return path(workspaceDir, name); }

void showModal(const std::string& title, const std::string& body) {
    modalTitle = title;
    modalBody = body;
    modalOpen = true;
    dirty = true;
}

void loadFile(const std::string& name) {
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
    startMenu = false;
    editorOpen = true;
    oskOpen = false;
    loadFile(name);
    SDL_StartTextInput();
}

void closeEditor() {
    if (childPid > 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, nullptr, 0);
        childPid = -1;
    }
    if (childOutput >= 0) { close(childOutput); childOutput = -1; }
    editorOpen = false;
    oskOpen = false;
    SDL_StopTextInput();
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
    consoleText = "Executando " + activeName + "...\n";
    dirty = true;
}

void runCurrentFile() {
    saveFile();
    if (activeName.size() >= 3 && activeName.substr(activeName.size() - 3) == ".py")
        startProgram("python", filePath(activeName));
    else if (activeName.size() >= 3 && activeName.substr(activeName.size() - 3) == ".js")
        startProgram("node", filePath(activeName));
}

void pollChild() {
    if (childPid <= 0) return;
    char buf[512];
    bool changed = false;
    for (;;) {
        ssize_t n = read(childOutput, buf, sizeof(buf));
        if (n > 0) {
            if (consoleText.size() < 8000) consoleText.append(buf, static_cast<std::size_t>(n));
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
        childPid = -1;
        consoleText += "\n[processo encerrado]";
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
    if (startMenu) {
        menuSelection = (menuSelection + dy + 6) % 6;
    } else if (editorOpen && oskOpen) {
        int count = keyboardKeyCount();
        oskSelection = (oskSelection + dx + (dy ? dy * 10 : 0) + count * 10) % count;
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
        focus = (focus + dx + 5) % 5;
    }
    dirty = true;
}

void activate(int item) {
    startMenu = false;
    switch (item) {
        case 0: runAppScript("Terminal"); break;
        case 1: runAppScript("FileManager"); break;
        case 2: openEditor("hello.py"); break;
        case 3: openEditor("hello.py"); runCurrentFile(); break;
        case 4: openEditor("hello.js"); runCurrentFile(); break;
        default: running = false; break;
    }
    dirty = true;
}

SDL_Rect cardRect(int item) { return SDL_Rect{78 + item * 226, 310, 204, 174}; }

int itemAt(int x, int y) {
    for (int i = 0; i < 5; ++i) {
        SDL_Rect r = cardRect(i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
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

void click(int x, int y) {
    if (modalOpen) { modalOpen = false; dirty = true; return; }
    if (editorOpen) {
        if (oskOpen) {
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
            return;
        }
        if (x >= 969 && x < 1085 && y >= 91 && y < 142) { saveFile(); return; }
        if (x >= 1085 && x < 1198 && y >= 91 && y < 142) { runCurrentFile(); return; }
        if (x >= 68 && x < 270 && y >= 169 && y < 224) { loadFile("hello.py"); return; }
        if (x >= 68 && x < 270 && y >= 226 && y < 281) { loadFile("hello.js"); return; }
        if (x >= 1108 && x < 1203 && y >= 32 && y < 68) { closeEditor(); return; }
        if (x >= 865 && x < 969 && y >= 91 && y < 142) { oskOpen = !oskOpen; dirty = true; return; }
        return;
    }
    if (startMenu) {
        if (x >= 52 && x < 370 && y >= 288 && y < 576) {
            const int row = (y - 288) / 48;
            if (row >= 0 && row < 6) activate(row);
        } else { startMenu = false; dirty = true; }
        return;
    }
    if (y >= 640 && x < 90) { startMenu = !startMenu; dirty = true; return; }
    if (y >= 640 && x < 180) { startMenu = !startMenu; dirty = true; return; }
    const int item = itemAt(x, y);
    if (item >= 0) { focus = item; activate(item); return; }
    if (x > 1146 && y >= 640) { showModal("Sair do Desktop", "Feche este app para voltar ao CrossMix."); return; }
    if (x > 600 && y > 530 && y < 625) { openEditor("hello.py"); return; }
}

void drawBackground() {
    color(ink); SDL_RenderClear(renderer);
    for (int i = 0; i < 40; ++i) {
        Uint8 r = static_cast<Uint8>(13 + i / 4);
        Uint8 g = static_cast<Uint8>(20 + i / 3);
        Uint8 b = static_cast<Uint8>(39 + i * 2);
        fill(0, i * 17, W, 18, Color{r, g, b, 255});
    }
    fill(748, 88, 360, 2, Color{87, 102, 188, 70});
}

void drawHome() {
    drawBackground();
    fill(0, 0, W, 52, Color{10, 15, 28, 225});
    drawText("CROSSMIX", 42, 16, 18, white);
    drawText("DESKTOP  /  AMBIENTE DE DESENVOLVIMENTO", 174, 18, 14, muted);
    drawText(clockText(), 1100, 17, 16, white);

    fill(45, 72, 1190, 534, Color{17, 27, 47, 220});
    outline(45, 72, 1190, 534, Color{57, 76, 111, 255});
    drawText("Seu espaco de desenvolvimento", 78, 103, 32, white);
    drawText("Um desktop leve, controlado pelo console, teclado ou mouse.", 80, 150, 18, muted);
    fill(1010, 102, 188, 66, Color{35, 52, 83, 255});
    drawText("Python 3.11", 1028, 112, 18, green);
    drawText("incluido no CrossMix", 1028, 139, 13, muted);

    const std::array<Color, 5> accents{{blue, violet, green, Color{250, 184, 89, 255}, Color{121, 183, 231, 255}}};
    const std::array<std::string, 5> glyphs{{">_", "F", "{}", "Py", "JS"}};
    const std::array<std::string, 5> titles{{"Terminal", "Arquivos", "Editor", "Python", "Node.js"}};
    const std::array<std::string, 5> subtitles{{"Shell do CrossMix", "Dingux Commander", "Workspace local", "Executar projetos", "Em preparacao"}};
    for (int i = 0; i < 5; ++i) {
        SDL_Rect r = cardRect(i);
        card(r.x, r.y, r.w, r.h, accents[i], glyphs[i], titles[i], subtitles[i], focus == i);
    }

    fill(78, 515, 1085, 66, Color{25, 38, 62, 230});
    drawText("Workspace", 96, 529, 14, muted);
    drawText("hello.py", 96, 550, 19, white);
    drawText("F5 executa Python  |  Ctrl+S salva  |  F1 abre o teclado virtual", 302, 544, 15, muted);

    fill(0, 640, W, 80, Color{9, 13, 25, 245});
    fill(24, 649, 54, 54, startMenu ? blue : Color{42, 61, 95, 255});
    drawText("D", 43, 659, 28, white);
    drawText("Apps", 96, 663, 16, white);
    fill(157, 653, 2, 36, Color{69, 82, 111, 255});
    drawText("Terminal", 180, 663, 16, muted);
    drawText("Arquivos", 290, 663, 16, muted);
    drawText("Editor", 402, 663, 16, muted);
    drawText("A: abrir    B: voltar    START: menu", 730, 663, 14, muted);
    drawText("TrimUI Smart Pro", 1064, 663, 14, white);

    if (startMenu) {
        fill(34, 194, 354, 420, Color{15, 24, 42, 252});
        outline(34, 194, 354, 420, Color{80, 104, 151, 255});
        drawText("Iniciar", 58, 210, 25, white);
        drawText("APLICATIVOS E FERRAMENTAS", 58, 251, 12, muted);
        const std::array<std::string, 6> labels{{"Terminal", "Arquivos", "Editor de codigo", "Python 3.11", "Node.js", "Voltar ao CrossMix"}};
        for (int i = 0; i < 6; ++i) {
            const int y = 288 + i * 48;
            if (menuSelection == i) fill(52, y, 318, 42, Color{44, 67, 103, 255});
            drawText(labels[i], 70, y + 10, 18, i == 5 ? muted : white);
        }
    }
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
    drawText("CrossMix Code", 52, 39, 18, white);
    drawText(activeName, 210, 40, 16, muted);
    drawText("ESC: desktop", 1000, 40, 14, muted);
    fill(1108, 35, 95, 28, Color{43, 67, 103, 255}); drawText("Voltar", 1125, 41, 14, white);

    fill(49, 83, 218, 533, Color{19, 27, 41, 255});
    drawText("EXPLORER", 66, 100, 14, muted);
    drawText("WORKSPACE", 66, 134, 13, muted);
    fill(61, 163, 194, 42, activeName == "hello.py" ? Color{38, 61, 94, 255} : Color{22, 31, 47, 255});
    drawText("hello.py", 79, 174, 16, white);
    fill(61, 210, 194, 42, activeName == "hello.js" ? Color{38, 61, 94, 255} : Color{22, 31, 47, 255});
    drawText("hello.js", 79, 221, 16, white);
    drawText("/Data/Desktop/Workspace", 64, 574, 11, muted);

    fill(267, 83, 948, 533, Color{16, 22, 34, 255});
    fill(267, 83, 948, 42, Color{23, 31, 47, 255});
    drawText(activeName, 292, 96, 16, white);
    fill(865, 91, 96, 28, Color{41, 57, 82, 255}); drawText("Teclado", 879, 97, 14, white);
    fill(969, 91, 108, 28, Color{41, 57, 82, 255}); drawText("Salvar", 995, 97, 14, white);
    fill(1085, 91, 112, 28, Color{38, 106, 94, 255}); drawText("Executar", 1105, 97, 14, white);

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
            drawText(std::to_string(lineNo), 285, y, 15, Color{101, 117, 143, 255});
            Color code = (line.find("print") != std::string::npos || line.find("console") != std::string::npos)
                ? Color{126, 190, 255, 255} : Color{214, 222, 237, 255};
            drawText(line, 326, y, 16, code);
            if (lineNo - 1 == cursorLine) {
                const std::string prefix = line.substr(0, std::min<std::size_t>(cursorCol, line.size()));
                const int cx = 326 + textWidth(prefix, 16);
                fill(cx, y + 1, 2, 19, blue);
            }
            if (end == std::string::npos) break;
            pos = end + 1; y += 23; ++lineNo;
        }
        fill(267, 490, 948, 126, Color{13, 19, 30, 255});
        drawText("SAIDA", 286, 500, 12, muted);
        if (consoleText.empty()) drawText("F5 executa o arquivo atual. A saida aparece aqui.", 286, 523, 14, muted);
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
                drawText(lines[i], 286, oy, 14, childPid > 0 ? green : muted);
        }
    } else drawOsk();

    fill(0, 640, W, 80, Color{9, 13, 25, 245});
    drawText("CrossMix Desktop", 38, 664, 16, white);
    drawText("SELECT: teclado    X: executar    Y: salvar    ESC/B: voltar", 376, 664, 14, muted);
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
    if (editorOpen) drawEditor(); else drawHome();
    if (modalOpen) drawModal();
    if (!capturePath.empty() && !captured) {
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
    else if (startMenu) startMenu = false;
    else if (editorOpen) closeEditor();
    else running = false;
    dirty = true;
}

void activateFocused() {
    if (modalOpen) { modalOpen = false; dirty = true; return; }
    if (editorOpen && oskOpen) {
        const auto rows = keyboardRows();
        int index = oskSelection;
        for (const auto& row : rows) {
            if (index < static_cast<int>(row.size())) { typeKey(row[index]); return; }
            index -= static_cast<int>(row.size());
        }
        return;
    }
    if (startMenu) { activate(menuSelection); return; }
    if (editorOpen) { return; }
    activate(focus);
}

void keyDown(SDL_Keycode key, SDL_Keymod mod) {
    if (modalOpen) { modalOpen = false; dirty = true; return; }
    if (editorOpen) {
        if (oskOpen && (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT)) {
            navigate(key == SDLK_LEFT ? -1 : key == SDLK_RIGHT ? 1 : 0, key == SDLK_UP ? -1 : key == SDLK_DOWN ? 1 : 0);
            return;
        }
        editorKey(key, mod);
        return;
    }
    if (key == SDLK_F1 || key == SDLK_HOME || key == SDLK_LGUI || key == SDLK_RGUI) {
        startMenu = !startMenu; dirty = true; return;
    }
    if (startMenu) {
        if (key == SDLK_UP) menuSelection = (menuSelection + 5) % 6;
        else if (key == SDLK_DOWN) menuSelection = (menuSelection + 1) % 6;
        else if (key == SDLK_RETURN || key == SDLK_SPACE) activate(menuSelection);
        else if (key == SDLK_ESCAPE) startMenu = false;
        dirty = true; return;
    }
    if (key == SDLK_LEFT) navigate(-1, 0);
    else if (key == SDLK_RIGHT) navigate(1, 0);
    else if (key == SDLK_RETURN || key == SDLK_SPACE) activateFocused();
    else if (key == SDLK_ESCAPE) doBack();
    else if (key == SDLK_TAB) { focus = (focus + 1) % 5; dirty = true; }
}

void hat(int x, int y) {
    if (!x && !y) return;
    const unsigned int now = SDL_GetTicks();
    if (now - lastPadRepeat < 115) return;
    lastPadRepeat = now;
    navigate(x, y);
}

void event(const SDL_Event& e) {
    if (e.type == SDL_QUIT) { running = false; return; }
    if (e.type == SDL_WINDOWEVENT) { dirty = true; return; }
    if (e.type == SDL_MOUSEMOTION) { cursorX = e.motion.x; cursorY = e.motion.y; dirty = true; }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) click(e.button.x, e.button.y);
    if (e.type == SDL_MOUSEWHEEL && editorOpen) { navigate(0, e.wheel.y > 0 ? -1 : 1); }
    if (e.type == SDL_TEXTINPUT && editorOpen) insertText(e.text.text);
    if (e.type == SDL_KEYDOWN && e.key.repeat == 0) keyDown(e.key.keysym.sym, static_cast<SDL_Keymod>(e.key.keysym.mod));
    if (e.type == SDL_JOYHATMOTION) {
        int x = (e.jhat.value & SDL_HAT_LEFT) ? -1 : (e.jhat.value & SDL_HAT_RIGHT) ? 1 : 0;
        int y = (e.jhat.value & SDL_HAT_UP) ? -1 : (e.jhat.value & SDL_HAT_DOWN) ? 1 : 0;
        hat(x, y);
    }
    if (e.type == SDL_JOYAXISMOTION) {
        if (e.jaxis.axis == 0) axisX = e.jaxis.value;
        if (e.jaxis.axis == 1) axisY = e.jaxis.value;
    }
    if (e.type == SDL_JOYBUTTONDOWN) {
        if (e.jbutton.button == 0) activateFocused();
        else if (e.jbutton.button == 1) doBack();
        else if (e.jbutton.button == 2 && editorOpen) saveFile();
        else if (e.jbutton.button == 3 && editorOpen) runCurrentFile();
        else if (e.jbutton.button == 6 && editorOpen) { oskOpen = !oskOpen; dirty = true; }
        else if (e.jbutton.button == 7) {
            if (editorOpen) closeEditor();
            else { startMenu = !startMenu; dirty = true; }
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
    mkdir(dataDir.c_str(), 0775);
    mkdir(workspaceDir.c_str(), 0775);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1;
    }
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "SDL_ttf: %s\n", TTF_GetError()); SDL_Quit(); return 1;
    }
    const bool console = env("CROSSMIX_DESKTOP_PLATFORM", "console") != "preview";
    const Uint32 flags = console ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    window = SDL_CreateWindow("CrossMix Desktop", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, flags);
    if (!window) { std::fprintf(stderr, "Janela: %s\n", SDL_GetError()); TTF_Quit(); SDL_Quit(); return 1; }
    renderer = SDL_CreateRenderer(window, -1, console ? SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC : SDL_RENDERER_SOFTWARE);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) { std::fprintf(stderr, "Renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1; }
    SDL_RenderSetLogicalSize(renderer, W, H);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_ShowCursor(SDL_ENABLE);
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

    while (running) {
        const uint64_t now = SDL_GetTicks64();
        const float dt = lastPointerTick ? std::min(0.05f, (now - lastPointerTick) / 1000.0f) : 0.0f;
        lastPointerTick = now;
        if (std::abs(axisX) > 8000 || std::abs(axisY) > 8000) {
            cursorX = std::clamp(cursorX + static_cast<int>((axisX / 32767.0f) * 650.0f * dt), 0, W - 1);
            cursorY = std::clamp(cursorY + static_cast<int>((axisY / 32767.0f) * 650.0f * dt), 0, H - 1);
            SDL_WarpMouseInWindow(window, cursorX, cursorY);
            dirty = true;
        }
        pollChild();
        if (now - lastClockDraw > 1000) { lastClockDraw = now; if (!editorOpen) dirty = true; }
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, childPid > 0 ? 40 : 120)) { event(e); while (SDL_PollEvent(&e)) event(e); }
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
