#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include "json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <vector>
#include "ui_audio.hpp"
#include "console_art.hpp"

using J = nlohmann::json;
using std::string;
constexpr int W=1280, H=720;
const string ROOT = std::getenv("BIGPICTURE_SDCARD") ? std::getenv("BIGPICTURE_SDCARD") : "/mnt/SDCARD";
const string DATA=ROOT+"/Data/BigPicture", APP=ROOT+"/Apps/BigPicture";
bool device = std::getenv("BIGPICTURE_PLATFORM") && string(std::getenv("BIGPICTURE_PLATFORM"))=="trimui" && access("/etc/openwrt_release",F_OK)==0;
volatile sig_atomic_t stopRequested=0;
void requestStop(int) { stopRequested=1; }
uint64_t ticks() { return SDL_GetTicks64(); }
J readJson(const string& path, J fallback=J::object()) {
    try { std::ifstream f(path); if(f) { J j; f>>j; return j; } } catch(const std::exception& e) { std::cerr<<path<<": "<<e.what()<<"\n"; }
    return fallback;
}
void saveJson(const string& path,const J& j) {
    std::ofstream f(path+".tmp"); f<<j.dump(2)<<"\n"; f.close();
    if(!f || rename((path+".tmp").c_str(),path.c_str())!=0) std::cerr<<"Nao foi possivel salvar "<<path<<"\n";
}
std::vector<string> files(const string& path) {
    std::vector<string> out; DIR* dir=opendir(path.c_str()); if(!dir) return out;
    while(auto* ent=readdir(dir)) if(ent->d_name[0]!='.') out.push_back(ent->d_name);
    closedir(dir); std::sort(out.begin(),out.end()); return out;
}
pid_t spawn(const std::vector<string>& args,const string& directory="") {
    pid_t pid=fork();
    if(pid==0) { setpgid(0,0); if(!directory.empty()&&chdir(directory.c_str())!=0)_exit(126); std::vector<char*> argv; for(const auto& s:args) argv.push_back(const_cast<char*>(s.c_str())); argv.push_back(nullptr); execvp(argv[0],argv.data()); _exit(127); }
    return pid;
}
struct WakeGate {
    bool sleeping=false, fnOff=false; string policy="any";
    bool accepts(const string& button) const { return (!fnOff && (button=="power" || policy=="any" || policy==button)) || button=="fn_on"; }
};
struct Item { string id,title,system,rom,launcher,art,kind,systemId,background; };
struct Group { string id,title,art; std::vector<string> members; };
struct Movie { string path,title,thumbnail; };
struct Image { SDL_Texture* texture=nullptr; SDL_Texture* blur=nullptr; int w=1,h=1; };
struct BackgroundLayer { string path; double opacity; };

struct Frontend {
    SDL_Window* window=nullptr; SDL_Renderer* render=nullptr;
    std::map<int,TTF_Font*> fonts; std::map<string,SDL_Texture*> textCache; std::map<string,Image> imageCache;
    std::vector<Item> games,apps; std::vector<Movie> movies; std::vector<int> shown;
    std::vector<Group> consoles,groups;
    std::vector<SDL_Joystick*> joysticks; std::vector<int> inputs;
    J settings,state; WakeGate gate; std::mt19937 rng{std::random_device{}()};
    string page="home", toast, query, modal, sleepReason; bool menu=false, running=true, searching=false, fnOn=true, dirty=true;
    int selected=0, tab=0, menuIndex=0, option=0, videoIndex=0, collectionIndex=0, sortMode=0, lastMovie=-1;
    int consoleIndex=0, homeZone=0, homeTab=0, homeTile=0, videoFocus=1, navX=0,navY=0;
    uint64_t nextNav=0;
    bool discardKeyRepeats=false, tabsFocused=false;
    double homeScroll=0, homeFocus=0;
    string homeTitleId;
    double homeTitleProgress=1;
    uint64_t homeTitleStart=0;
    uint64_t homeAnimationTime=0;
    bool homeAnimationReady=false, homeAnimating=false;
    std::vector<BackgroundLayer> homeBackgrounds;
    SDL_Texture* backgroundShade=nullptr;
    std::map<string,SDL_Texture*> backdropCache;
    std::vector<string> backdropOrder;
    std::map<int,double> homeHighlights;
    std::map<string,ConsoleArt> consoleArtCache;
    std::vector<string> consoleArtOrder;
    int consoleZone=0,detailReturnConsoleZone=0;
    double consoleScroll=0,consoleNameProgress=1;
    std::map<int,double> consoleHighlights;
    string consoleNameId;
    uint64_t consoleAnimationTime=0,consoleNameStart=0;
    bool consoleAnimationReady=false,consoleAnimating=false;
    UiAudio audio;
    uint64_t lastUiSound=0;
    string lastUiSoundName;
    int batteryPercent=-1, masterVolumePercent=65;
    uint64_t lastStatusPoll=0, volumeOverlayUntil=0, volumeSettleUntil=0;
    uint64_t movieRetryAt=0;
    bool movieFromWake=false, pendingMovieRetry=false;
    int wakeMovieRetries=0;
    Item detailItem;
    string detailReturnPage;
    int detailReturnSelected=0,detailReturnTab=0;
    bool detailReturnTabs=false,detailClosing=false;
    double detailProgress=0;
    uint64_t detailTime=0;
    pid_t child=-1; string childType; uint64_t lastInput=0, toastUntil=0, ignoreUntil=0, gameStarted=0, nextFrame=0;
    Item launched; int idleSeconds=120;

    void trace(const string& action,const string& detail="") {
        J status={{"event",action},{"detail",detail},{"sleeping",gate.sleeping},{"fn_on",fnOn},{"page",page},{"movie_active",childType=="movie"},{"platform",device?"trimui":"desktop"},{"pid",getpid()},{"time",std::time(nullptr)}};
        saveJson(DATA+"/runtime-status.json",status);
        std::ofstream f(DATA+"/events.jsonl",std::ios::app); f<<status.dump()<<"\n";
    }
    void notify(const string& text) { toast=text; toastUntil=ticks()+3500; dirty=true; }
    void persist() { saveJson(DATA+"/settings.json",settings); saveJson(DATA+"/library-state.json",state); }
    void load() {
        J defaults={{"idle_seconds",120},{"wake_mode","any"},{"fn_enabled",true},{"intro_on_start",true},{"intro_on_wake",true},{"random_intro",true},{"selected_video",""},{"intro_volume",65},{"ui_sounds",true},{"ui_volume",65},{"master_volume",65}};
        settings=readJson(DATA+"/settings.json"); for(auto it=defaults.begin();it!=defaults.end();++it) if(!settings.contains(it.key())) settings[it.key()]=it.value();
        if(!settings.contains("video_mode"))settings["video_mode"]=settings.value("random_intro",true)?"all":"single";
        if(!settings.contains("enabled_videos")||!settings["enabled_videos"].is_array())settings["enabled_videos"]=J::array();
        state=readJson(DATA+"/library-state.json",{{"favorites",J::array()},{"recent",J::array()},{"play_seconds",J::object()}});
        for(const auto& key:{"favorites","recent"}) if(!state.contains(key)||!state[key].is_array()) state[key]=J::array();
        if(!state.contains("play_seconds")) state["play_seconds"]=J::object();
        idleSeconds=settings.value("idle_seconds",120); gate.policy=settings.value("wake_mode",string("any"));
        if(const char* idle=std::getenv("BIGPICTURE_IDLE_SECONDS")) if(*idle) idleSeconds=std::max(1,atoi(idle));
        J cat=readJson(DATA+"/catalog.json");
        auto items=[&](const char* key,std::vector<Item>& dest) { if(!cat.contains(key)||!cat[key].is_array()) return; for(const auto& j:cat[key]) dest.push_back({j.value("id",""),j.value("title",""),j.value("system",""),j.value("rom",""),j.value("launcher",""),j.value("art",""),j.value("kind","game"),j.value("system_id",""),j.value("background",j.value("art",""))}); };
        items("games",games); items("apps",apps);
        auto loadGroups=[&](const char* key,std::vector<Group>& dest){if(cat.contains(key)&&cat[key].is_array())for(const auto& j:cat[key])dest.push_back({j.value("id",""),j.value("title",""),j.value("art",""),j.value("games",std::vector<string>{})});};
        loadGroups("consoles",consoles);loadGroups("collections",groups);
        string lastConsole=state.value("last_console",string(""));bool restored=false;
        for(int i=0;i<int(consoles.size());++i){string custom=APP+"/consoles/"+consoles[i].id+".png";if(access(custom.c_str(),R_OK)==0)consoles[i].art=custom;if(consoles[i].id==lastConsole){consoleIndex=i;restored=true;}}
        if(!restored)for(int i=0;i<int(consoles.size());++i){bool available=false;for(const auto& game:games)if(game.systemId==consoles[i].id){available=true;break;}if(available){consoleIndex=i;break;}}
        J manifests=readJson(DATA+"/videos/startup/manifest.json",J::array());
        for(const auto& name:files(DATA+"/videos/startup")) {
            if(name.size()<4) continue;
            auto ext=name.substr(name.find_last_of('.')+1);
            if(ext!="mp4"&&ext!="webm"&&ext!="mkv") continue;
            string title=name.substr(0,name.find_last_of('.'));
            if(manifests.is_array()) for(const auto& j:manifests) if(j.value("file","")==name) title=j.value("title",title);
            movies.push_back({DATA+"/videos/startup/"+name,title,DATA+"/videos/startup/"+name.substr(0,name.find_last_of('.'))+".jpg"});
        }
        for(const auto& name:files("/dev/input")) if(name.rfind("event",0)==0) { int fd=open(("/dev/input/"+name).c_str(),O_RDONLY|O_NONBLOCK); if(fd>=0) inputs.push_back(fd); }
        refreshStatus(true);
    }
    bool favorite(const string& id) const { for(const auto& x:state["favorites"]) if(x==id) return true; return false; }
    void uiSound(const string& name){
        if(!settings.value("ui_sounds",true))return;
        uint64_t now=ticks();if(name==lastUiSoundName&&now-lastUiSound<35)return;
        audio.volume(settings.value("ui_volume",65)*masterVolumePercent/100);if(audio.play(name)){lastUiSound=now;lastUiSoundName=name;}
    }
    int readInteger(const string& path,int fallback) const {
        std::ifstream file(path);int value=0;return file>>value?value:fallback;
    }
    int readCommandInteger(const string& command,int fallback) const {
        FILE* pipe=popen(command.c_str(),"r");if(!pipe)return fallback;
        char buffer[64]{};int value=fallback;if(std::fgets(buffer,sizeof(buffer),pipe))value=std::atoi(buffer);pclose(pipe);return value;
    }
    void refreshStatus(bool force=false) {
        uint64_t now=ticks();if(!force&&now-lastStatusPoll<1800)return;lastStatusPoll=now;
        int oldBattery=batteryPercent,oldVolume=masterVolumePercent;
        if(device) {
            batteryPercent=std::clamp(readInteger("/sys/class/power_supply/axp2202-battery/capacity",-1),-1,100);
            if(now>=volumeSettleUntil) {int raw=readCommandInteger("/usr/trimui/bin/shmvar vol 2>/dev/null",-1);if(raw>=0)masterVolumePercent=std::clamp(raw*5,0,100);}
        } else {
            batteryPercent=-1;masterVolumePercent=std::clamp(settings.value("master_volume",65),0,100);
        }
        if(oldBattery!=batteryPercent||oldVolume!=masterVolumePercent)dirty=true;
    }
    void adjustMasterVolume(int direction,int steps=1) {
        if(device) {
            int raw=std::clamp((masterVolumePercent+2)/5+direction*steps,0,20);
            std::ofstream output("/tmp/system/set_volume");if(output)output<<raw<<"\n";
            masterVolumePercent=raw*5;volumeSettleUntil=ticks()+900;
        } else {
            masterVolumePercent=std::clamp(masterVolumePercent+direction*5*steps,0,100);
            settings["master_volume"]=masterVolumePercent;persist();
        }
        audio.volume(settings.value("ui_volume",65)*masterVolumePercent/100);
        volumeOverlayUntil=ticks()+1900;dirty=true;trace("volume_changed",std::to_string(masterVolumePercent));
    }
    void toggleFavorite() {
        if(shown.empty() || tabsFocused || (page!="home"&&page!="library"&&page!="favorites"&&page!="collection"&&page!="console"&&page!="consoles"&&page!="recents") || (page=="home"&&homeZone!=0)||(page=="consoles"&&consoleZone!=1)) return;
        const auto& item=games[shown[selected]];
        auto& f=state["favorites"]; if(favorite(item.id)) { for(auto it=f.begin();it!=f.end();++it) if(*it==item.id) { f.erase(it);break; } notify("Removido dos favoritos"); } else { f.push_back(item.id);notify("Adicionado aos favoritos"); }
        persist(); rebuild();
    }
    void rebuild() {
        shown.clear(); const auto& all=page=="apps"?apps:games;
        for(size_t i=0;i<all.size();++i) {
            if(page!="apps" && (tab==1||page=="favorites")&&!favorite(all[i].id)) continue;
            if((page=="console"||page=="consoles")&&(consoles.empty()||all[i].systemId!=consoles[consoleIndex].id))continue;
            if(page=="collection"&&(groups.empty()||std::find(groups[collectionIndex].members.begin(),groups[collectionIndex].members.end(),all[i].id)==groups[collectionIndex].members.end()))continue;
            string title=all[i].title, needle=query; std::transform(title.begin(),title.end(),title.begin(),[](unsigned char c){return std::tolower(c);}); std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return std::tolower(c);});
            if(!needle.empty()&&title.find(needle)==string::npos) continue;
            shown.push_back(int(i));
        }
        if(page=="home"||page=="recents"||sortMode==1) {
            auto rank=[&](int i) { int n=0; for(const auto& id:state["recent"]) { if(id==all[i].id)return n; ++n;} return 10000+i; };
            std::stable_sort(shown.begin(),shown.end(),[&](int a,int b){return rank(a)<rank(b);});
        } else std::stable_sort(shown.begin(),shown.end(),[&](int a,int b){return all[a].title<all[b].title;});
        if(!shown.empty()) selected=std::min(selected,int(shown.size())-1); else {selected=0;if(hasTabs()&&page!="consoles")tabsFocused=true;} dirty=true;
    }
    bool hasTabs() const {return page=="library"||page=="favorites"||page=="apps"||page=="consoles";}
    bool hasTabItems() const {return page=="consoles"?!consoles.empty():!shown.empty();}
    void go(const string& target) { page=target; selected=0; option=0; homeZone=0;videoFocus=1;consoleZone=0;tabsFocused=hasTabs()&&target!="consoles";menu=false; modal.clear(); query.clear(); tab=target=="favorites"?1:target=="consoles"?2:target=="apps"?3:0; rebuild();if(target=="home")resetHomeAnimation(ticks());if(target=="consoles")resetConsoleAnimation(ticks());trace("page",target); }
    SDL_Color color(int r,int g,int b,int a=255) { return {Uint8(r),Uint8(g),Uint8(b),Uint8(a)}; }
    void rect(int x,int y,int w,int h,SDL_Color c) { SDL_SetRenderDrawColor(render,c.r,c.g,c.b,c.a); SDL_Rect r{x,y,w,h}; SDL_RenderFillRect(render,&r); }
    void line(int x1,int y1,int x2,int y2,SDL_Color c) { SDL_SetRenderDrawColor(render,c.r,c.g,c.b,c.a);SDL_RenderDrawLine(render,x1,y1,x2,y2); }
    void rounded(int x,int y,int w,int h,int rad,SDL_Color c) {
        rect(x+rad,y,w-2*rad,h,c); rect(x,y+rad,w,h-2*rad,c);
        for(int dy=0;dy<rad;++dy) { int dx=int(std::sqrt(rad*rad-(rad-dy)*(rad-dy))); rect(x+rad-dx,y+dy,w-2*rad+2*dx,1,c);rect(x+rad-dx,y+h-1-dy,w-2*rad+2*dx,1,c); }
    }
    TTF_Font* font(int size,bool bold=false) { int key=size*2+bold; if(!fonts.count(key)) { fonts[key]=TTF_OpenFont((APP+"/font.ttf").c_str(),size); if(fonts[key]&&bold) TTF_SetFontStyle(fonts[key],TTF_STYLE_BOLD); } return fonts[key]; }
    void text(string value,int x,int y,int size,SDL_Color c,bool bold=false,int maxWidth=0) {
        TTF_Font* f=font(size,bold); if(!f||value.empty())return;
        if(maxWidth>0) {
            int tw=0;bool shortened=false;TTF_SizeUTF8(f,value.c_str(),&tw,nullptr);
            while(tw>maxWidth&&!value.empty()) {size_t end=value.size()-1;while(end>0&&(value[end]&0xC0)==0x80)--end;value.erase(end);shortened=true;TTF_SizeUTF8(f,(value+"…").c_str(),&tw,nullptr);}
            if(shortened)value+="…";
        }
        string key=std::to_string(size)+":"+std::to_string(bold)+":"+value;
        if(!textCache.count(key)) { if(textCache.size()>900) { for(auto& kv:textCache)SDL_DestroyTexture(kv.second);textCache.clear(); } auto* s=TTF_RenderUTF8_Blended(f,value.c_str(),color(255,255,255)); if(!s)return; textCache[key]=SDL_CreateTextureFromSurface(render,s);SDL_FreeSurface(s); }
        auto* t=textCache[key];int tw=0,th=0;SDL_QueryTexture(t,nullptr,nullptr,&tw,&th); SDL_SetTextureColorMod(t,c.r,c.g,c.b);SDL_SetTextureAlphaMod(t,c.a);SDL_Rect dst{x,y,tw,th};SDL_RenderCopy(render,t,nullptr,&dst);
    }
    Image& image(const string& path) {
        auto found=imageCache.find(path);if(found!=imageCache.end())return found->second;
        if(imageCache.size()>100) { for(auto& kv:imageCache){SDL_DestroyTexture(kv.second.texture);SDL_DestroyTexture(kv.second.blur);} imageCache.clear(); }
        Image result; auto* src=IMG_Load(path.c_str());
        if(src) {
            result.w=src->w;result.h=src->h;float scale=std::min(1.0f,600.0f/std::max(src->w,src->h));
            auto* scaled=SDL_CreateRGBSurfaceWithFormat(0,std::max(1,int(src->w*scale)),std::max(1,int(src->h*scale)),32,SDL_PIXELFORMAT_RGBA32);
            SDL_BlitScaled(src,nullptr,scaled,nullptr);result.texture=SDL_CreateTextureFromSurface(render,scaled);SDL_FreeSurface(scaled);
            auto* tiny=SDL_CreateRGBSurfaceWithFormat(0,24,16,32,SDL_PIXELFORMAT_RGBA32);SDL_BlitScaled(src,nullptr,tiny,nullptr);result.blur=SDL_CreateTextureFromSurface(render,tiny);SDL_FreeSurface(tiny);SDL_FreeSurface(src);
        }
        return imageCache.emplace(path,result).first->second;
    }
    void picture(const string& path,SDL_Rect dst,bool fit=false,int alpha=255,bool blur=false) {
        auto& img=image(path);auto* texture=blur?img.blur:img.texture;if(!texture)return;
        SDL_SetTextureAlphaMod(texture,Uint8(alpha)); SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        int tw=0,th=0;SDL_QueryTexture(texture,nullptr,nullptr,&tw,&th); SDL_Rect src{0,0,tw,th};
        float aspect=float(tw)/th, box=float(dst.w)/dst.h;
        if(fit) { if(aspect>box) {int h=int(dst.w/aspect);dst.y+=(dst.h-h)/2;dst.h=h;}else{int w=int(dst.h*aspect);dst.x+=(dst.w-w)/2;dst.w=w;} }
        else if(aspect>box) {src.w=int(th*box);src.x=(tw-src.w)/2;}else{src.h=int(tw/box);src.y=(th-src.h)/2;}
        SDL_RenderCopy(render,texture,&src,&dst);
    }
    void outline(SDL_Rect r,SDL_Color c,int width=2) { SDL_SetRenderDrawColor(render,c.r,c.g,c.b,c.a);for(int i=0;i<width;++i){SDL_Rect box{r.x-i,r.y-i,r.w+2*i,r.h+2*i};SDL_RenderDrawRect(render,&box);} }
    void card(const Item& item,SDL_Rect r,bool focus) {
        rect(r.x,r.y,r.w,r.h,color(22,31,41)); auto& img=image(item.art);
        if(img.texture) {
            picture(item.art,r,false,150,true);
            bool portrait=img.h>img.w*1.2f;
            picture(item.art,r,!portrait);
            if(!portrait) {rect(r.x,r.y+r.h-51,r.w,51,color(4,9,15,225));text(item.title,r.x+10,r.y+r.h-43,15,color(245,246,248),true,r.w-18);text(item.system,r.x+10,r.y+r.h-22,11,color(160,174,188),false,r.w-18);}
        } else {text(item.title,r.x+12,r.y+r.h/2-20,16,color(220,232,243),true,r.w-24);}
        if(focus) {outline({r.x-4,r.y-4,r.w+8,r.h+8},color(86,92,96),2);outline(r,color(234,240,244),2);}
        if(favorite(item.id)) text("★",r.x+r.w-28,r.y+8,21,color(102,203,247));
    }
    void batteryIcon(int x,int y) {
        SDL_Color edge=color(205,215,225),fill=batteryPercent>=0&&batteryPercent<=20?color(245,92,87):color(120,207,159);
        rect(x,y+3,19,13,color(26,36,46));outline({x,y+3,19,13},edge,1);rect(x+19,y+7,3,5,edge);
        if(batteryPercent>=0)rect(x+2,y+5,std::max(0,15*batteryPercent/100),9,fill);
    }
    void speakerIcon(int x,int y,SDL_Color c) {
        rect(x,y+7,4,8,c);line(x+4,y+7,x+10,y+2,c);line(x+4,y+15,x+10,y+20,c);line(x+10,y+2,x+10,y+20,c);
        line(x+13,y+6,x+16,y+9,c);line(x+16,y+9,x+16,y+13,c);line(x+16,y+13,x+13,y+16,c);
    }
    void volumeOverlay() {
        if(ticks()>=volumeOverlayUntil)return;
        int x=948,y=78,w=292,h=74;rounded(x,y,w,h,9,color(11,18,27,238));
        speakerIcon(x+16,y+16,color(224,235,245));text("VOLUME",x+43,y+13,13,color(167,190,211),true);
        text(std::to_string(masterVolumePercent)+"%",x+w-65,y+11,18,color(245,248,251),true);
        rounded(x+17,y+44,w-34,10,4,color(54,69,83));int fill=(w-38)*masterVolumePercent/100;
        if(fill>0)rounded(x+19,y+46,fill,6,3,color(79,183,231));
    }
    void header(bool search=true) {
        rect(0,0,W,66,color(5,9,12,225));
        if(search) {for(int a=0;a<360;a+=8){double rad=a*3.14159265/180;int x=26+int(cos(rad)*8),y=24+int(sin(rad)*8);rect(x,y,2,2,color(244,245,246));}line(32,31,39,38,color(244,245,246));text(query.empty()?"Busque jogos ou sistemas…":query,53,17,18,color(157,165,174));}
        else text("STEAM  /  TRIMUI",32,19,16,color(191,204,217),true);
        std::time_t now=std::time(nullptr);std::tm tm{};localtime_r(&now,&tm);char clock[10];strftime(clock,sizeof(clock),"%H:%M",&tm);
        batteryIcon(887,19);text(batteryPercent<0?"--%":std::to_string(batteryPercent)+"%",914,18,13,color(218,226,234),true);
        speakerIcon(972,17,color(210,221,231));text(std::to_string(masterVolumePercent)+"%",994,18,13,color(218,226,234),true);
        text(clock,1081,16,21,color(247,248,248),true);
        rounded(1190,11,58,43,7,color(29,63,83));text("TR",1204,20,19,color(124,211,255),true);
    }
    void footer() {
        rect(0,H-43,W,43,color(7,11,16,245));text("☰  MENU",28,H-32,15,color(242,244,246),true);
        text(page=="videos"?(videoMode()=="single"?"X  FIXAR":"X  MARCAR"):"Y  FAVORITO",W-630,H-32,13,color(185,194,204),true);if(page!="detail")text(page=="consoles"?"L1/R1 CONSOLES":page=="videos"?"L/R  MODOS":"L/R  ABAS",W-472,H-32,12,color(185,194,204),true);text(page=="detail"?"A  JOGAR":"A  SELECIONAR",W-320,H-32,13,color(243,245,247),true);text("B  VOLTAR",W-142,H-32,13,color(243,245,247),true);
    }
    double homeCardX(int index) const {return index==0?36.0:496.0+(index-1)*182.0;}
    double homeCardWidth(int index) const {return index==0?440.0:164.0;}
    double homeScrollTarget() const {
        if(shown.empty())return 0;
        return std::max(0.0,homeCardX(selected)+homeCardWidth(selected)/2.0-W/2.0);
    }
    string homeArt() const {
        return selected>=0&&selected<int(shown.size())?games[shown[selected]].background:string();
    }
    void resetHomeAnimation(uint64_t now) {
        homeScroll=homeScrollTarget();homeFocus=selected;homeBackgrounds.clear();
        homeTitleId=shown.empty()?string():games[shown[selected]].id;homeTitleProgress=1;homeTitleStart=now;
        homeHighlights.clear();homeHighlights[selected]=1.0;
        if(!homeArt().empty())homeBackgrounds.push_back({homeArt(),1.0});
        homeAnimationTime=now;homeAnimationReady=true;homeAnimating=false;
    }
    void advanceHomeAnimation(uint64_t now) {
        if(!homeAnimationReady){resetHomeAnimation(now);return;}
        uint64_t elapsed=now>=homeAnimationTime?now-homeAnimationTime:0;homeAnimationTime=now;
        double interval=double(std::min<uint64_t>(elapsed,100));
        double cardEase=1.0-std::exp(-interval/65.0);
        double bgEase=1.0-std::exp(-interval/180.0);
        double targetScroll=homeScrollTarget(),targetFocus=selected;
        string titleId=shown.empty()?string():games[shown[selected]].id;
        if(titleId!=homeTitleId){homeTitleId=titleId;homeTitleStart=now;homeTitleProgress=titleId.empty()?1:0;}
        if(!titleId.empty()&&homeTitleProgress<1){double age=double(now>=homeTitleStart?now-homeTitleStart:0);homeTitleProgress=std::clamp((age-120.0)/140.0,0.0,1.0);}
        homeScroll+=(targetScroll-homeScroll)*cardEase;homeFocus+=(targetFocus-homeFocus)*cardEase;
        if(std::abs(targetScroll-homeScroll)<0.15)homeScroll=targetScroll;
        if(std::abs(targetFocus-homeFocus)<0.002)homeFocus=targetFocus;
        if(!homeHighlights.count(selected))homeHighlights[selected]=0;
        for(auto& item:homeHighlights){double goal=item.first==selected?1:0;item.second+=(goal-item.second)*cardEase;if(std::abs(goal-item.second)<.002)item.second=goal;}
        for(auto it=homeHighlights.begin();it!=homeHighlights.end();){if(it->first!=selected&&it->second==0)it=homeHighlights.erase(it);else ++it;}
        string target=homeArt();bool found=false;
        for(const auto& layer:homeBackgrounds)if(layer.path==target)found=true;
        if(!target.empty()&&!found)homeBackgrounds.push_back({target,0.0});
        for(auto& layer:homeBackgrounds){double goal=layer.path==target?1.0:0.0;layer.opacity+=(goal-layer.opacity)*bgEase;if(std::abs(goal-layer.opacity)<0.002)layer.opacity=goal;}
        homeBackgrounds.erase(std::remove_if(homeBackgrounds.begin(),homeBackgrounds.end(),[&](const BackgroundLayer& layer){return layer.path!=target&&layer.opacity==0.0;}),homeBackgrounds.end());
        bool moving=homeScroll!=targetScroll||homeFocus!=targetFocus||homeTitleProgress<1;
        for(const auto& item:homeHighlights)if(item.second!=(item.first==selected?1.0:0.0))moving=true;
        for(const auto& layer:homeBackgrounds)if(layer.opacity!=(layer.path==target?1.0:0.0))moving=true;
        if(moving||homeAnimating)dirty=true;
        homeAnimating=moving;
    }
    int homeTitleAlpha() const {double p=homeTitleProgress;return int(std::round(255.0*p*p*(3.0-2.0*p)));}
    int homeTitleOffset() const {return int(std::round(14.0*std::pow(1.0-homeTitleProgress,3.0)));}
    SDL_Texture* backdrop(const string& path) {
        auto found=backdropCache.find(path);
        if(found!=backdropCache.end()){backdropOrder.erase(std::remove(backdropOrder.begin(),backdropOrder.end(),path),backdropOrder.end());backdropOrder.push_back(path);return found->second;}
        if(!SDL_RenderTargetSupported(render)||!image(path).blur)return nullptr;
        auto* texture=SDL_CreateTexture(render,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,W,H);
        if(!texture)return nullptr;
        auto* previous=SDL_GetRenderTarget(render);
        if(SDL_SetRenderTarget(render,texture)!=0){SDL_DestroyTexture(texture);return nullptr;}
        SDL_SetRenderDrawColor(render,0,0,0,0);SDL_RenderClear(render);
        picture(path,{0,25,W,H-25},false,255,false);
        SDL_SetRenderTarget(render,previous);SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        if(backdropOrder.size()>=6){string oldest=backdropOrder.front();backdropOrder.erase(backdropOrder.begin());SDL_DestroyTexture(backdropCache[oldest]);backdropCache.erase(oldest);}
        backdropCache[path]=texture;backdropOrder.push_back(path);return texture;
    }
    void background() {
        rect(0,0,W,H,color(7,12,18));
        const auto& pool=page=="apps"?apps:games;
        if(page=="home") {for(const auto& layer:homeBackgrounds)if(layer.opacity>0.001){auto* texture=backdrop(layer.path);if(texture){SDL_SetTextureAlphaMod(texture,Uint8(100*layer.opacity));SDL_Rect dst{0,0,W,H};SDL_RenderCopy(render,texture,nullptr,&dst);}else picture(layer.path,{0,25,W,H-25},false,int(100*layer.opacity),true);}}
        else if(page!="collections"&&page!="consoles"&&selected<int(shown.size())&&shown[selected]<int(pool.size())) picture(pool[shown[selected]].art,{0,25,W,H-25},false,60,true);
        if(!backgroundShade){auto* shade=SDL_CreateRGBSurfaceWithFormat(0,W,H,32,SDL_PIXELFORMAT_RGBA32);if(shade){for(int y=0;y<H;++y){int alpha=std::min(245,100+std::abs(y-270)/3);SDL_Rect row{0,y,W,1};SDL_FillRect(shade,&row,SDL_MapRGBA(shade->format,4,9,15,alpha));}backgroundShade=SDL_CreateTextureFromSurface(render,shade);SDL_FreeSurface(shade);if(backgroundShade)SDL_SetTextureBlendMode(backgroundShade,SDL_BLENDMODE_BLEND);}}
        if(backgroundShade){SDL_Rect dst{0,0,W,H};SDL_RenderCopy(render,backgroundShade,nullptr,&dst);}
    }
    void home() {
        header(false);text("Jogos recentes",36,83,26,color(222,226,231),true);
        if(shown.empty()) text("Adicione seus jogos na pasta Roms do cartão.",42,190,24,color(180,195,210));
        else {
        int count=int(shown.size());selected=std::min(selected,count-1);
        const Item& hero=games[shown[selected]];
        SDL_Rect clip{0,123,W,279};SDL_RenderSetClipRect(render,&clip);
        auto drawHomeCard=[&](int i){double x=homeCardX(i)-homeScroll,w=homeCardWidth(i);if(x+w< -20||x>W+20)return;
            double weight=homeHighlights.count(i)?homeHighlights.at(i):0;int grow=int(std::round(weight*18.0));
            SDL_Rect box{int(std::round(x))-int(w*weight*.035),139-grow/2,int(w)+int(w*weight*.07),247+grow};
            card(games[shown[i]],box,false);
            if(homeZone==0&&i==selected){outline({box.x-5,box.y-5,box.w+10,box.h+10},color(115,125,136,Uint8(110+145*weight)),2);outline({box.x-2,box.y-2,box.w+4,box.h+4},color(238,242,246,Uint8(110+145*weight)),2);}};
        for(int i=0;i<count;++i)if(i!=selected)drawHomeCard(i);
        drawHomeCard(selected);
        SDL_RenderSetClipRect(render,nullptr);
        int titleX=int(std::clamp(homeCardX(selected)-homeScroll,38.0,920.0));
        int titleY=homeTitleOffset(),titleAlpha=homeTitleAlpha();
        text(hero.title,titleX,404+titleY,24,color(245,246,248,titleAlpha),true,W-titleX-40);
        int seconds=state["play_seconds"].value(hero.id,0);
        text(seconds?"TEMPO DE JOGO: "+std::to_string(seconds/60)+" MIN":"PRONTO PARA JOGAR  •  "+hero.system,titleX,439+titleY,14,color(151,178,157,titleAlpha),true,W-titleX-40);
        }
        std::array<string,4> tabs={"RECENTES","FAVORITOS","COLEÇÕES","CONSOLES"};
        for(int i=0;i<4;++i){int x=254+i*194;bool focus=homeZone==1&&homeTab==i;
            if(homeTab==i)rounded(x,483,180,40,18,color(focus?67:39,focus?87:49,focus?106:60));
            text(tabs[i],x+24,495,14,color(230,236,242),true);if(focus)outline({x,483,180,40},color(142,220,255),2);
        }
        std::array<string,4> titles={"SUA BIBLIOTECA","SEUS FAVORITOS","CONSOLES","VÍDEOS DE INÍCIO"};
        std::array<string,4> details={std::to_string(games.size())+" jogos",std::to_string(state["favorites"].size())+" favoritos",std::to_string(consoles.size())+" sistemas",std::to_string(movies.size())+" animações"};
        for(int i=0;i<4;++i) {int x=36+i*307;rounded(x,545,287,112,6,color(18+i*2,30+i*2,44+i*3));text(titles[i],x+17,567,15,color(183,208,232),true);text(details[i],x+17,602,22,color(245,246,248),true);if(homeZone==2&&homeTile==i)outline({x,545,287,112},color(142,220,255),2);}
    }
    void libraryTabs() {
        std::array<string,4> labels={"TODOS OS JOGOS  "+std::to_string(games.size()),"FAVORITOS  "+std::to_string(state["favorites"].size()),"CONSOLES  "+std::to_string(consoles.size()),"APLICATIVOS  "+std::to_string(apps.size())};
        for(int i=0;i<4;++i) {int x=104+i*288;if(i==tab)rounded(x,79,275,38,18,color(45,49,54));text(labels[i],x+15,91,13,color(i==tab?240:151,i==tab?242:158,i==tab?245:166),true,251);if(i==tab&&tabsFocused)outline({x,79,275,38},color(142,220,255),2);}
    }
    void openDetail(){
        if(shown.empty())return;
        detailItem=games[shown[selected]];detailReturnPage=page;detailReturnSelected=selected;detailReturnTab=tab;detailReturnTabs=tabsFocused;detailReturnConsoleZone=consoleZone;
        page="detail";detailClosing=false;detailProgress=0;detailTime=ticks();dirty=true;uiSound("detail");trace("game_detail",detailItem.id);
    }
    void restoreDetail(){
        page=detailReturnPage;tab=detailReturnTab;tabsFocused=detailReturnTabs;selected=detailReturnSelected;rebuild();
        for(int i=0;i<int(shown.size());++i)if(games[shown[i]].id==detailItem.id){selected=i;break;}
        if(page=="home")resetHomeAnimation(ticks());
        if(page=="consoles"){consoleZone=detailReturnConsoleZone;resetConsoleAnimation(ticks());}
        detailClosing=false;dirty=true;trace("detail_return",detailItem.id);
    }
    void advanceDetail(uint64_t now){
        double elapsed=double(now>=detailTime?std::min<uint64_t>(now-detailTime,100):0);detailTime=now;
        detailProgress=std::clamp(detailProgress+(detailClosing?-1.0:1.0)*elapsed/180.0,0.0,1.0);
        if(detailClosing&&detailProgress==0){restoreDetail();return;}
        if(detailProgress<1||detailClosing)dirty=true;
    }
    void detailPage(){
        int offset=int((1-detailProgress)*48);
        picture(detailItem.background,{0,-offset,W,474},false,int(255*detailProgress));
        for(int y=290;y<475;y+=4)rect(0,y,W,4,color(8,15,22,std::clamp((y-290)*255/185,0,255)));
        text(detailItem.title,40,370-offset,32,color(246,249,252),true,1180);
        int top=474+int((1-detailProgress)*130);rect(0,top,W,H-top,color(30,41,52));
        rounded(40,top+24,229,60,4,color(81,153,37));text("▶  JOGAR",77,top+41,22,color(247,252,245),true);
        text("SISTEMA",309,top+27,12,color(154,174,193),true);text(detailItem.system,309,top+53,18,color(225,236,246),true,330);
        text("TEMPO DE JOGO",680,top+27,12,color(154,174,193),true);int seconds=state["play_seconds"].value(detailItem.id,0);text(std::to_string(seconds/60)+" min",680,top+53,18,color(225,236,246),true);
        text(favorite(detailItem.id)?"★  NOS FAVORITOS":"☆  ADICIONAR AOS FAVORITOS",40,top+117,16,color(174,214,242),true);
        header(false);
    }
    void library() {
        header();
        if(page=="collection"||page=="console"||page=="recents") text(page=="recents"?"Jogos recentes":page=="console"?consoles[consoleIndex].title:groups[collectionIndex].title,44,84,23,color(236,241,246),true);
        else libraryTabs();
        const auto& pool=page=="apps"?apps:games;
        if(shown.empty()) {
            if(page=="console"&&!consoles.empty()) {text("Nenhum jogo encontrado para este console.",48,194,23,color(184,200,216));text("Adicione jogos em Roms/"+consoles[consoleIndex].id+" e reinicie a interface.",48,240,18,color(153,178,200),false,1160);}
            else text("Nenhum item nesta seleção.",48,194,24,color(184,200,216));
            return;
        }
        int first=(selected/14)*14;
        for(int n=0;n<14&&first+n<int(shown.size());++n) {
            int x=49+(n%7)*171,y=143+(n/7)*262;
            card(pool[shown[first+n]],{x,y,148,222},first+n==selected&&!tabsFocused);
        }
        const auto& current=pool[shown[selected]];
        text(current.title,49,642,17,color(219,229,239),true,1030);
        text(std::to_string(selected+1)+" / "+std::to_string(shown.size()),1131,644,14,color(162,180,198));
    }
    double consoleScrollTarget() const {return std::max(0.0,184.0+consoleIndex*300.0-W/2.0);}
    void resetConsoleAnimation(uint64_t now){
        consoleScroll=consoleScrollTarget();consoleAnimationTime=now;consoleAnimationReady=true;consoleAnimating=false;
        consoleNameId=consoles.empty()?string():consoles[consoleIndex].id;consoleNameStart=now;consoleNameProgress=1;consoleHighlights.clear();consoleHighlights[consoleIndex]=1;
    }
    void changeConsole(int direction){
        if(consoles.empty())return;
        int next=std::clamp(consoleIndex+direction,0,int(consoles.size())-1);if(next==consoleIndex)return;
        consoleIndex=next;state["last_console"]=consoles[consoleIndex].id;selected=0;query.clear();rebuild();if(consoleZone==1&&shown.empty())consoleZone=0;dirty=true;
    }
    void advanceConsoleAnimation(uint64_t now){
        if(!consoleAnimationReady){resetConsoleAnimation(now);return;}
        double interval=double(now>=consoleAnimationTime?std::min<uint64_t>(now-consoleAnimationTime,100):0);consoleAnimationTime=now;
        double ease=1-std::exp(-interval/65.0),target=consoleScrollTarget();consoleScroll+=(target-consoleScroll)*ease;if(std::abs(target-consoleScroll)<.15)consoleScroll=target;
        string id=consoles.empty()?string():consoles[consoleIndex].id;if(id!=consoleNameId){consoleNameId=id;consoleNameStart=now;consoleNameProgress=id.empty()?1:0;}
        if(consoleNameProgress<1){double age=double(now>=consoleNameStart?now-consoleNameStart:0);consoleNameProgress=std::clamp((age-120.0)/140.0,0.0,1.0);}
        if(!consoleHighlights.count(consoleIndex))consoleHighlights[consoleIndex]=0;
        bool moving=consoleScroll!=target||consoleNameProgress<1;
        for(auto it=consoleHighlights.begin();it!=consoleHighlights.end();){double goal=it->first==consoleIndex?1:0;it->second+=(goal-it->second)*ease;if(std::abs(goal-it->second)<.002)it->second=goal;if(it->second!=goal)moving=true;if(it->first!=consoleIndex&&it->second==0)it=consoleHighlights.erase(it);else ++it;}
        if(moving||consoleAnimating)dirty=true;
        consoleAnimating=moving;
    }
    ConsoleArt& consoleArt(const string& path){
        auto found=consoleArtCache.find(path);if(found!=consoleArtCache.end()){consoleArtOrder.erase(std::remove(consoleArtOrder.begin(),consoleArtOrder.end(),path),consoleArtOrder.end());consoleArtOrder.push_back(path);return found->second;}
        if(consoleArtOrder.size()>=12){string oldest=consoleArtOrder.front();consoleArtOrder.erase(consoleArtOrder.begin());freeConsoleArt(consoleArtCache[oldest]);consoleArtCache.erase(oldest);}
        consoleArtOrder.push_back(path);return consoleArtCache.emplace(path,makeConsoleArt(render,path)).first->second;
    }
    void consoleBrowser(){
        header();libraryTabs();
        if(consoles.empty()){text("Nenhum console disponível neste cartão.",40,180,23,color(183,203,224));return;}
        SDL_Rect clip{0,130,W,218};SDL_RenderSetClipRect(render,&clip);
        auto drawConsole=[&](int i){double center=184+i*300-consoleScroll;if(center< -170||center>W+170)return;
            auto& art=consoleArt(consoles[i].art);double weight=consoleHighlights.count(i)?consoleHighlights.at(i):0,scale=1+.07*weight;
            SDL_Rect box{int(std::round(center-art.w*scale/2)),int(std::round(240-art.h*scale/2)),int(std::round(art.w*scale)),int(std::round(art.h*scale))};
            if(art.image){SDL_Rect shadow=box;shadow.x+=4;shadow.y+=8;if(art.shadow){SDL_SetTextureAlphaMod(art.shadow,230);SDL_RenderCopy(render,art.shadow,nullptr,&shadow);}
                bool active=i==consoleIndex&&!tabsFocused;if(art.outline){SDL_SetTextureColorMod(art.outline,active?155:109,active?220:137,active?255:161);SDL_SetTextureAlphaMod(art.outline,active?255:100);SDL_RenderCopy(render,art.outline,nullptr,&box);}SDL_RenderCopy(render,art.image,nullptr,&box);}
            else text(consoles[i].title,int(center)-110,220,20,color(205,223,240),true,220);
        };
        for(int i=0;i<int(consoles.size());++i){if(i!=consoleIndex)drawConsole(i);}
        drawConsole(consoleIndex);SDL_RenderSetClipRect(render,nullptr);
        const auto& current=consoles[consoleIndex];double p=consoleNameProgress;int alpha=int(std::round(255*p*p*(3-2*p))),offset=int(std::round(14*std::pow(1-p,3)));
        int titleWidth=0;TTF_Font* titleFont=font(23,true);if(titleFont)TTF_SizeUTF8(titleFont,current.title.c_str(),&titleWidth,nullptr);
        int titleX=int(std::clamp(184+consoleIndex*300-consoleScroll-titleWidth/2.0,36.0,std::max(36.0,W-titleWidth-36.0)));
        text(current.title,titleX,351+offset,23,color(241,247,252,alpha),true,W-titleX-30);text(std::to_string(shown.size())+" jogos",titleX,386+offset,14,color(157,183,207,alpha));
        text("Jogos",40,432,19,color(216,230,242),true);text(std::to_string(consoleIndex+1)+" / "+std::to_string(consoles.size()),1122,435,13,color(153,180,204));
        if(shown.empty()){text("Este console ainda não tem jogos.",40,491,21,color(177,198,219));text("Adicione em Roms/"+current.id+" e reinicie a interface.",40,532,16,color(140,165,188),false,1160);return;}
        int first=(selected/8)*8;for(int n=0;n<8&&first+n<int(shown.size());++n)card(games[shown[first+n]],{49+n*148,468,130,174},consoleZone==1&&!tabsFocused&&first+n==selected);
        text(games[shown[selected]].title,49,649,15,color(210,226,240),true,1020);text(std::to_string(selected+1)+" / "+std::to_string(shown.size()),1130,651,12,color(153,181,205));
    }
    void collections() {
        bool byConsole=page=="consoles";const auto& all=byConsole?consoles:groups;
        header();if(byConsole)libraryTabs();else text("Coleções",40,84,29,color(242,246,250),true);
        if(all.empty()){text(byConsole?"Nenhum console disponível neste cartão.":"Nenhuma coleção disponível neste cartão.",42,180,22,color(183,203,224));return;}
        for(int i=0;i<8&&i<int(all.size());++i) {
            int absolute=(selected/8)*8+i;if(absolute>=int(all.size()))break;
            int x=42+(i%4)*310,y=150+(i/4)*242;int count=0;const auto& group=all[absolute];
            for(const auto& game:games)if(byConsole?game.systemId==group.id:std::find(group.members.begin(),group.members.end(),game.id)!=group.members.end())count++;
            rounded(x,y,286,212,7,color(24,37,53));picture(group.art,{x+10,y+10,266,139},true,190);text(group.title,x+16,y+157,19,color(238,244,250),true,254);text(std::to_string(count)+" jogos",x+16,y+185,13,color(150,173,193));
            if(absolute==selected&&!tabsFocused)outline({x-3,y-3,292,218},color(239,244,249),2);
        }
        text(std::to_string(selected+1)+" / "+std::to_string(all.size()),1125,640,14,color(160,185,206));
    }
    string movieName(int index) const {const auto& path=movies[index].path;return path.substr(path.find_last_of('/')+1);}
    bool movieMarked(int index) const {for(const auto& name:settings["enabled_videos"])if(name==movieName(index))return true;return false;}
    string videoMode() const {return settings.value("video_mode",string("all"));}
    string videoModeLabel() const {return videoMode()=="selected"?"Aleatório: marcados":videoMode()=="single"?"Um vídeo fixo":"Aleatório: todos";}
    void changeVideoMode(int direction) {
        std::array<string,3> modes={"all","selected","single"};int index=videoMode()=="selected"?1:videoMode()=="single"?2:0;
        index=(index+direction+3)%3;settings["video_mode"]=modes[index];settings["random_intro"]=index!=2;
        if(index==2&&!movies.empty()&&settings.value("selected_video",string("")).empty())settings["selected_video"]=movieName(videoIndex);
        persist();dirty=true;
    }
    void selectVideo() {
        if(movies.empty())return;
        if(videoMode()=="single"){settings["selected_video"]=movieName(videoIndex);notify("Vídeo fixo escolhido");}
        else {auto& marked=settings["enabled_videos"];string name=movieName(videoIndex);bool remove=movieMarked(videoIndex);
            if(remove){for(auto it=marked.begin();it!=marked.end();++it)if(*it==name){marked.erase(it);break;}}
            else marked.push_back(name);
            notify(remove?"Vídeo desmarcado":"Vídeo marcado para o sorteio personalizado");}
        persist();dirty=true;
    }
    std::vector<int> moviePool() const {
        std::vector<int> pool;for(int i=0;i<int(movies.size());++i)if(videoMode()!="selected"||movieMarked(i))pool.push_back(i);
        if(videoMode()=="single"&&!movies.empty()){for(int i:pool)if(movieName(i)==settings.value("selected_video",string("")))return {i};return {0};}
        return pool;
    }
    void videoPage() {
        header(false);text("Vídeos de início",40,86,30,color(241,245,249),true);
        text("Escolha o modo. Qualquer botão ou direção pula a animação.",41,130,16,color(157,177,195));
        std::array<string,3> modes={"ALEATÓRIO: TODOS","ALEATÓRIO: MARCADOS","UM VÍDEO FIXO"};int mode=videoMode()=="selected"?1:videoMode()=="single"?2:0;
        for(int i=0;i<3;++i){int x=40+i*403;if(mode==i)rounded(x,165,385,37,12,color(50,68,85));text(modes[i],x+18,176,15,color(229,239,247),true);if(videoFocus==0&&mode==i)outline({x,165,385,37},color(142,220,255),2);}
        if(movies.empty()) {text("Salve MP4 ou WebM em Data/BigPicture/videos/startup",40,230,21,color(218,230,240));return;}
        videoIndex=std::min(videoIndex,int(movies.size())-1);
        for(int row=0;row<7;++row) {int i=(videoIndex/7)*7+row;if(i>=int(movies.size()))break;int y=222+row*54;
            if(i==videoIndex)rounded(35,y-4,475,48,5,color(56,67,78));
            text(movies[i].title,53,y+9,17,color(233,239,246),i==videoIndex,397);
            bool chosen=videoMode()=="single"?settings.value("selected_video",string(""))==movieName(i):movieMarked(i);
            if(chosen)text("✓",473,y+7,20,color(108,213,248));
            if(i==videoIndex&&videoFocus==1)outline({35,y-4,475,48},color(142,220,255),1);
        }
        auto& movie=movies[videoIndex];rounded(550,218,687,347,8,color(12,19,28));picture(movie.thumbnail,{563,231,661,321},true);
        text("A  REPRODUZIR",566,583,15,color(226,235,243),true);text(videoMode()=="single"?"X  ESCOLHER FIXO":"X  MARCAR / DESMARCAR",817,583,14,color(153,205,234),true);
        string detail=videoMode()=="selected"?std::to_string(moviePool().size())+" vídeos marcados":videoMode()=="single"?"Fixo: "+settings.value("selected_video",string("")):"Sorteio entre todos os "+std::to_string(movies.size())+" vídeos";
        text(detail,566,616,14,color(153,177,196),false,650);text(std::to_string(videoIndex+1)+" / "+std::to_string(movies.size())+"  •  ↑ no primeiro vídeo: escolher modo",42,636,13,color(155,180,201));
    }
    std::vector<std::pair<string,string>> options() {
        string wake=settings.value("wake_mode",string("any"));
        return {{"Apagar a tela por inatividade",idleSeconds?std::to_string(idleSeconds)+" segundos":"Desativado"},
                {"Despertar com",wake=="any"?"Qualquer botão":wake=="power"?"Botão Power":"Interruptor Fn"},
                {"Fn desliga / liga a tela",settings.value("fn_enabled",true)?"Ativado":"Desativado"},
                {"Vídeo ao abrir a interface",settings.value("intro_on_start",true)?"Ativado":"Desativado"},
                {"Vídeo ao retornar do standby",settings.value("intro_on_wake",true)?"Ativado":"Desativado"},
                {"Modo das animações",videoModeLabel()},
                {"Volume dos vídeos",std::to_string(settings.value("intro_volume",65))+"%"},
                {"Ver / escolher vídeos",std::to_string(movies.size())+" vídeos"},
                {"Iniciar ao ligar o console",access((ROOT+"/trimui/app/.bigpicture-boot-installed").c_str(),F_OK)==0?"Ativado":"Desativado"},
                {"Registrar opção no editor Fn",device?"Abrir integração":"Disponível no TrimUI"},
                {"Entrar em standby agora","Tela apagada"},
                {"Sons da interface",settings.value("ui_sounds",true)?"Ativados":"Desativados"},
                {"Volume da interface",std::to_string(settings.value("ui_volume",65))+"%"},
                {"Configurações nativas do TrimUI",device?"Abrir System Tools":"Disponível no TrimUI"}};
    }
    void settingsPage() {
        header(false);text("Configurações",40,85,30,color(238,244,250),true);text("Vídeos, inatividade e retorno",42,129,16,color(155,176,197));
        auto all=options();int first=(option/10)*10;for(int i=first;i<std::min(first+10,int(all.size()));++i){int y=168+(i-first)*42;if(i==option)rounded(32,y-2,1216,40,5,color(48,60,73));text(all[i].first,53,y+7,17,color(233,240,246),i==option);text(all[i].second,920,y+7,16,color(150,205,234),false,300);}
        text(std::to_string(option+1)+" / "+std::to_string(all.size()),1120,615,14,color(155,177,198));
        text(device?"A tela é apagada sem encerrar o launcher.":"Prévia PC: standby apaga somente esta janela.",45,638,14,color(147,166,184));
    }
    void sidebar() {
        rect(0,0,W,H,color(0,0,0,165));rect(0,66,280,H-109,color(15,22,30,252));
        std::array<string,10> labels={"Início","Biblioteca","Favoritos","Coleções","Consoles","Aplicativos","Vídeos de início","Configurações","Energia","Voltar ao CrossMix"};
        if(!device)labels[9]="Fechar interface";
        std::array<string,10> icons={"⌂","▦","★","▤","◈","▣","▶","⚙","◯","←"};
        for(int i=0;i<10;++i){int y=124+i*55;if(i==menuIndex)rect(0,y-8,280,53,color(62,72,83));text(icons[i],24,y+3,24,color(190,204,216));text(labels[i],66,y+4,19,color(i==menuIndex?245:171,i==menuIndex?247:184,i==menuIndex?250:197),i==menuIndex,204);}
    }
    void draw() {
        if(gate.sleeping) {rect(0,0,W,H,color(0,0,0));SDL_RenderPresent(render);dirty=false;return;}
        background();
        if(page=="home")home();else if(page=="consoles")consoleBrowser();else if(page=="detail")detailPage();else if(page=="videos")videoPage();else if(page=="settings")settingsPage();else if(page=="collections")collections();else library();
        footer();if(menu)sidebar();
        if(searching){rect(0,0,W,H,color(0,0,0,175));rounded(230,240,820,160,12,color(27,40,55));text("Buscar na biblioteca",260,261,24,color(236,244,251),true);text(query+"│",262,322,23,color(164,219,250),false,750);}
        if(!modal.empty()){rect(0,0,W,H,color(0,0,0,180));rounded(255,210,770,260,12,color(27,40,55));text("Steam Big Picture",286,233,25,color(230,240,248),true);text(modal,286,300,19,color(186,206,226),false,710);text("A / B  VOLTAR",286,416,15,color(148,207,239),true);}
        if(ticks()<toastUntil){rounded(310,H-105,660,45,8,color(42,59,76,245));text(toast,330,H-93,17,color(231,242,251),false,625);}
        volumeOverlay();
        SDL_RenderPresent(render);dirty=false;
    }
    void power(const string& action) {
        auto pid=spawn({"/bin/sh",APP+"/power.sh",action});if(pid>0){int status=0;waitpid(pid,&status,0);if(status!=0)std::cerr<<"Backlight command failed: "<<action<<"\n";}
    }
    void stopMovie() {
        if(child>0&&childType=="movie") {kill(-child,SIGTERM);int status=0;waitpid(child,&status,0);child=-1;childType.clear();audio.pause(false);SDL_ShowWindow(window);SDL_RaiseWindow(window);SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);lastInput=ticks();ignoreUntil=ticks()+400;navX=navY=0;discardKeyRepeats=true;dirty=true;trace("video_skipped");}
    }
    void movie(int manual=-1,bool fromWake=false) {
        if(movies.empty()) {notify("Adicione vídeos em Data/BigPicture/videos/startup");return;}
        if(child>0)return;
        int choice=manual;
        if(choice<0) {
            auto pool=moviePool();if(pool.empty()){notify("Marque ao menos um vídeo para o sorteio.");return;}
            if(pool.size()>1)pool.erase(std::remove(pool.begin(),pool.end(),lastMovie),pool.end());
            choice=pool[rng()%pool.size()];
        }
        if(choice<0||choice>=int(movies.size())||access(movies[choice].path.c_str(),R_OK)!=0){notify("O arquivo do vídeo de início não foi encontrado ou não pode ser lido.");return;}
        lastMovie=choice;movieFromWake=fromWake;audio.pause(true);SDL_HideWindow(window);
        string player=device?ROOT+"/System/bin/mpv":"/usr/bin/mpv";
        int movieVolume=settings.value("intro_volume",65);if(!device)movieVolume=movieVolume*masterVolumePercent/100;
        std::vector<string> args={player,"--no-terminal","--fullscreen","--force-window=yes","--ontop","--keep-open=no","--input-default-bindings=no","--title=Steam Big Picture - Introducao","--input-conf="+APP+"/video-input.conf","--volume="+std::to_string(movieVolume)};
        if(device) {args.push_back("--config-dir="+ROOT+"/Emus/VIDEOS/.config/mpv");args.push_back("--vo=sdl");args.push_back("--ao=sdl");args.push_back("--log-file="+DATA+"/video-player.log");}
        else {args.push_back("--osc=no");args.push_back("--no-config");}
        args.push_back(movies[choice].path);child=spawn(args);childType="movie";
        trace("video_start",movies[choice].path);
        if(child<0){childType.clear();audio.pause(false);SDL_ShowWindow(window);notify("Não foi possível abrir o vídeo.");}
    }
    void sleepScreen(bool byFn=false) {
        if(gate.sleeping) {if(byFn)gate.fnOff=true;return;}
        if(childType=="game")return;
        stopMovie();audio.pause(true);gate.sleeping=true;gate.fnOff=byFn;sleepReason=byFn?"fn":"idle_or_power";dirty=true;draw();power("off");trace("screen_off",sleepReason);
    }
    void wake(const string& button,bool powerIntro=false) {
        if(!gate.sleeping)return;
        if(button=="fn_on"){gate.fnOff=false;fnOn=true;}
        if(!gate.accepts(button))return;
        gate.sleeping=false;gate.fnOff=false;power("on");audio.pause(false);lastInput=ticks();ignoreUntil=ticks()+400;dirty=true;trace("wake",button);
        if(powerIntro||settings.value("intro_on_wake",true)) {
            wakeMovieRetries=0;pendingMovieRetry=false;
            SDL_Delay(220);draw();SDL_Delay(120);
            movie(-1,true);
        }
    }
    void fn(bool on) {
        fnOn=on;if(!settings.value("fn_enabled",true))return;
        if(on)wake("fn_on");else sleepScreen(true);
    }
    void launch() {
        if(shown.empty())return;
        const auto& pool=page=="apps"?apps:games;launched=page=="detail"?detailItem:pool[shown[selected]];
        if(!device) {
            static const std::set<string> supported={"ATARI2600","DOOM","FC","GB","GBC","GBA","PCE","SFC","VB"};
            bool app=launched.kind=="app";
            if(app&&launched.id!="app:RetroArch") {modal="Este aplicativo do TrimUI ainda não está disponível no PC.";dirty=true;return;}
            if(!app&&!supported.count(launched.systemId)) {modal="Este console ainda não tem emulador configurado na prévia PC.";dirty=true;return;}
            uiSound("launch");persist();SDL_HideWindow(window);
            child=spawn({"/bin/sh","/opt/dev-support/bigpicture/launch-pc.sh",app?"app":launched.systemId,launched.rom});
            childType="game";gameStarted=ticks();trace("game_start",launched.id);
            if(child<0){childType.clear();SDL_ShowWindow(window);notify("Não foi possível abrir no PC.");}return;
        }
        if(launched.launcher.rfind(ROOT+"/Emus/",0)!=0&&launched.launcher.rfind(ROOT+"/Apps/",0)!=0) {notify("Launcher inválido.");return;}
        if(access(launched.launcher.c_str(),R_OK)!=0){notify("Launcher não encontrado.");return;}
        uiSound("launch");SDL_Delay(90);audio.pause(true);persist();SDL_HideWindow(window);std::vector<string> args={"/bin/sh",launched.launcher};if(!launched.rom.empty())args.push_back(launched.rom);
        child=spawn(args,launched.launcher.substr(0,launched.launcher.find_last_of('/')));childType="game";gameStarted=ticks();trace("game_start",launched.id);
        if(child<0){childType.clear();audio.pause(false);SDL_ShowWindow(window);notify("Não foi possível abrir o aplicativo.");}
    }
    void pollChild() {
        if(child<=0)return;
        int status=0;pid_t result=waitpid(child,&status,WNOHANG);if(result<=0)return;
        string completed=childType;
        bool movieFailed=completed=="movie"&&(!WIFEXITED(status)||WEXITSTATUS(status)!=0);
        bool retryWakeMovie=movieFailed&&movieFromWake&&wakeMovieRetries==0;
        movieFromWake=false;
        bool crossMixHandoff=completed=="game"&&launched.id=="app:SystemTools"&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
        child=-1;childType.clear();
        if(crossMixHandoff) {
            trace("crossmix_handoff","SystemTools");
            running=false;
            return;
        }
        audio.pause(false);SDL_ShowWindow(window);SDL_RaiseWindow(window);
        SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);lastInput=ticks();ignoreUntil=ticks()+300;dirty=true;
        if(completed=="game"&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&launched.kind!="app") {
            auto& recent=state["recent"];J next=J::array();next.push_back(launched.id);for(const auto& id:recent)if(id!=launched.id&&next.size()<100)next.push_back(id);recent=next;
            state["play_seconds"][launched.id]=state["play_seconds"].value(launched.id,0)+int((ticks()-gameStarted)/1000);persist();if(page!="detail")rebuild();
        }
        bool skipped=completed=="movie"&&WIFEXITED(status)&&WEXITSTATUS(status)==42;
        if(completed=="movie")discardKeyRepeats=true;
        navX=navY=0;trace(skipped?"video_skipped":completed=="movie"?"video_end":"game_end",std::to_string(WIFEXITED(status)?WEXITSTATUS(status):-1));
        if(retryWakeMovie&&!skipped){wakeMovieRetries=1;pendingMovieRetry=true;movieRetryAt=ticks()+500;trace("video_wake_retry",std::to_string(lastMovie));}
        else if((!WIFEXITED(status)||WEXITSTATUS(status)!=0)&&!skipped)notify(completed=="movie"?"O vídeo não pôde ser reproduzido.":"O aplicativo encerrou com erro.");
    }
    void pollHardware() {
        if(device) for(int fd:inputs) {input_event e{};while(read(fd,&e,sizeof(e))==sizeof(e)) {
            if(e.type==EV_SW&&e.code==1)fn(e.value!=0);
            else if(e.type==EV_KEY&&(e.value==1||((e.code==KEY_VOLUMEUP||e.code==KEY_VOLUMEDOWN)&&e.value==2))) {
                lastInput=ticks();
                if(e.code==KEY_POWER&&e.value==1) {if(gate.sleeping)wake("power",true);else sleepScreen();}
                else if(e.code==KEY_VOLUMEUP||e.code==KEY_VOLUMEDOWN) {if(gate.sleeping)wake("any");else adjustMasterVolume(e.code==KEY_VOLUMEUP?1:-1,e.value==2?2:1);}
                else if(gate.sleeping)wake("any");else if(childType=="movie")stopMovie();
            }
        }}
        std::ifstream command("/tmp/bigpicture-fn");string value;if(command>>value){command.close();unlink("/tmp/bigpicture-fn");fn(value=="1");}
    }
    void optionChange(int direction=1) {
        if(option==0){std::vector<int> values={0,30,60,120,300,600};auto it=std::find(values.begin(),values.end(),idleSeconds);int index=it==values.end()?0:int(it-values.begin());index=(index+direction+int(values.size()))%int(values.size());idleSeconds=values[index];settings["idle_seconds"]=idleSeconds;}
        else if(option==1){std::vector<string> values={"any","power","fn"};string old=settings.value("wake_mode",string("any"));auto it=std::find(values.begin(),values.end(),old);int index=it==values.end()?0:int(it-values.begin());index=(index+direction+3)%3;settings["wake_mode"]=values[index];gate.policy=values[index];}
        else if(option>=2&&option<=4){const char* keys[]={"fn_enabled","intro_on_start","intro_on_wake"};const char* key=keys[option-2];settings[key]=!settings.value(key,true);}
        else if(option==5)changeVideoMode(direction);
        else if(option==6)settings["intro_volume"]=std::clamp(settings.value("intro_volume",65)+direction*5,0,100);
        else if(option==7)go("videos");
        else if(option==8||option==9) {
            if(!device)modal="Esta opção é aplicada no console TrimUI.";
            else {
                string tool=option==9?"install-fn.sh":access((ROOT+"/trimui/app/.bigpicture-boot-installed").c_str(),F_OK)==0?"disable-boot.sh":"enable-boot.sh";
                auto pid=spawn({"/bin/sh",APP+"/"+tool});int status=0;if(pid>0)waitpid(pid,&status,0);
                notify(status==0?(option==9?"Abra o editor Fn e selecione Big Picture: standby":"Configuração de inicialização atualizada"):"Não foi possível aplicar a opção");
            }
        }
        else if(option==10)sleepScreen();
        else if(option==11){bool enabled=settings.value("ui_sounds",true);if(enabled)uiSound("toggle_off");settings["ui_sounds"]=!enabled;if(!enabled)uiSound("toggle_on");}
        else if(option==12){settings["ui_volume"]=std::clamp(settings.value("ui_volume",65)+direction*5,0,100);uiSound("volume");}
        else if(option==13) {
            if(!device)notify("As configurações nativas ficam disponíveis no TrimUI.");
            else {
                auto it=std::find_if(apps.begin(),apps.end(),[](const Item& item){return item.id=="app:SystemTools";});
                if(it==apps.end()||access(it->launcher.c_str(),R_OK)!=0)notify("System Tools não foi encontrado no cartão.");
                else {
                    launched=*it;uiSound("launch");SDL_Delay(90);audio.pause(true);persist();SDL_HideWindow(window);
                    child=spawn({"/bin/sh",launched.launcher},launched.launcher.substr(0,launched.launcher.find_last_of('/')));childType="game";gameStarted=ticks();trace("native_settings_start","SystemTools");
                    if(child<0){childType.clear();audio.pause(false);SDL_ShowWindow(window);notify("Não foi possível abrir System Tools.");}
                }
            }
            return;
        }
        persist();dirty=true;lastInput=ticks();trace("settings_changed");
    }
    void accept() {
        if(!modal.empty()){modal.clear();dirty=true;return;}
        if(searching){searching=false;SDL_StopTextInput();goSearch();return;}
        if(menu){
            static const char* pages[]={"home","library","favorites","collections","consoles","apps","videos","settings"};
            if(menuIndex<8)go(pages[menuIndex]);else if(menuIndex==8){menu=false;sleepScreen();}else running=false;return;
        }
        if(page=="settings"){optionChange();return;}
        if(page=="detail"){if(!detailClosing)launch();return;}
        if(hasTabs()&&tabsFocused){if(hasTabItems())tabsFocused=false;dirty=true;return;}
        if(page=="home"&&homeZone>0){static const char* sections[]={"recents","favorites","collections","consoles"};static const char* tiles[]={"library","favorites","consoles","videos"};go(homeZone==1?sections[homeTab]:tiles[homeTile]);return;}
        if(page=="videos"){if(videoFocus==0){videoFocus=1;dirty=true;}else if(!movies.empty())movie(videoIndex);return;}
        if(page=="collections"){if(groups.empty())return;collectionIndex=selected;go("collection");return;}
        if(page=="consoles"){if(consoleZone==0){if(!shown.empty())consoleZone=1;dirty=true;}else openDetail();return;}
        if(page=="apps")launch();else openDetail();
    }
    void goSearch() {page="library";selected=0;tab=0;tabsFocused=false;rebuild();}
    void tabChange(int direction) {
        if(direction)uiSound("tab");
        tab=(tab+direction+4)%4;selected=0;
        if(tab==2){page="consoles";consoleZone=0;}else if(tab==3){page="apps";}else{page="library";}rebuild();if(page=="consoles")resetConsoleAnimation(ticks());if(!hasTabItems())tabsFocused=true;
    }
    void move(int x,int y) {
        if(menu){menuIndex=(menuIndex+y+10)%10;dirty=true;return;}
        if(page=="home") {if(y){homeZone=std::clamp(homeZone+y,0,2);dirty=true;return;}if(homeZone==1){homeTab=(homeTab+x+4)%4;dirty=true;return;}if(homeZone==2){homeTile=(homeTile+x+4)%4;dirty=true;return;}if(x&&!shown.empty()){selected=std::clamp(selected+x,0,int(shown.size())-1);dirty=true;return;}}
        if(page=="settings"){if(y)option=(option+y+int(options().size()))%int(options().size());if(x)optionChange(x);dirty=true;return;}
        if(page=="detail")return;
        if(page=="videos"){if(videoFocus==0){if(x)changeVideoMode(x);if(y>0)videoFocus=1;}else if(y<0&&videoIndex==0)videoFocus=0;else if(!movies.empty())videoIndex=std::clamp(videoIndex+y,0,int(movies.size())-1);dirty=true;return;}
        if(page=="consoles"){
            if(tabsFocused){if(x)tabChange(x);else if(y>0){tabsFocused=false;consoleZone=0;}dirty=true;return;}
            if(consoleZone==0){if(x)changeConsole(x);if(y<0)tabsFocused=true;else if(y>0){if(!shown.empty())consoleZone=1;else notify("Este console ainda não tem jogos.");}}
            else {if(y<0)consoleZone=0;else if(!shown.empty())selected=std::clamp(selected+x+(y>0?8:0),0,int(shown.size())-1);}
            dirty=true;return;
        }
        if(hasTabs()){
            if(tabsFocused){if(x)tabChange(x);else if(y>0&&hasTabItems())tabsFocused=false;dirty=true;return;}
            int columns=page=="consoles"?4:7;
            if(y<0&&selected<columns){tabsFocused=true;dirty=true;return;}
        }
        bool grid=page=="collections"||page=="consoles";
        int size=page=="collections"?int(groups.size()):page=="consoles"?int(consoles.size()):int(shown.size());if(!size)return;
        int stride=page=="home"?1:grid?4:7;int delta=x+y*stride;selected=(selected+delta+size)%size;dirty=true;
    }
    void button(const string& action) {
        if(action.empty())return;
        lastInput=ticks();
        if(action=="fn"){fn(!fnOn);return;}
        if(gate.sleeping){wake(action=="power"?"power":"any",action=="power");return;}
        if(ticks()<ignoreUntil)return;
        if(action=="power"){sleepScreen();return;}
        if(action=="volume_up"||action=="volume_down"){adjustMasterVolume(action=="volume_up"?1:-1);return;}
        if(childType=="movie"){stopMovie();ignoreUntil=ticks()+400;return;}
        if(action=="intro"){movie();return;}
        if(action=="menu"){if(searching){searching=false;SDL_StopTextInput();}else{menu=!menu;uiSound(menu?"menu_open":"menu_close");}dirty=true;return;}
        if(action=="back") {uiSound(menu?"menu_close":"back");if(!modal.empty())modal.clear();else if(searching){searching=false;SDL_StopTextInput();}else if(menu)menu=false;else if(page=="detail"){detailClosing=true;detailTime=ticks();}else if(page=="consoles"&&consoleZone==1){consoleZone=0;}else if(page=="console"){go("consoles");tabsFocused=false;}else if(page=="collection"){go("collections");selected=collectionIndex;}else if(page!="home")go("home");else if(homeZone>0)homeZone=0;else{menu=true;uiSound("menu_open");}dirty=true;return;}
        if(action=="ok"){bool detailAction=!menu&&modal.empty()&&!searching&&!tabsFocused&&(page=="home"?homeZone==0:page=="consoles"?consoleZone==1:page=="library"||page=="favorites"||page=="console"||page=="collection"||page=="recents");if(!detailAction&&page!="detail"&&page!="apps")uiSound("confirm");accept();return;}
        if(action=="favorite"){if(page=="detail"){auto& list=state["favorites"];bool remove=favorite(detailItem.id);if(remove){for(auto it=list.begin();it!=list.end();++it)if(*it==detailItem.id){list.erase(it);break;}}else list.push_back(detailItem.id);persist();dirty=true;}else if(page=="videos")selectVideo();else toggleFavorite();uiSound("confirm");return;}
        if(action=="search"){searching=true;query.clear();SDL_StartTextInput();dirty=true;return;}
        if(action=="sort"){sortMode=1-sortMode;rebuild();notify(sortMode?"Ordenado por recentes":"Ordenado por nome");return;}
        if(action=="next"||action=="prev"){if(page=="detail"||menu)return;int direction=action=="next"?1:-1;if(page=="consoles"){int previous=consoleIndex;changeConsole(direction);if(previous!=consoleIndex)uiSound("scroll");}else if(page=="videos"){uiSound("tab");changeVideoMode(direction);}else tabChange(direction);return;}
        if(!modal.empty()||searching)return;
        auto navState=[&](){return page+":"+std::to_string(selected)+":"+std::to_string(tab)+":"+std::to_string(tabsFocused)+":"+std::to_string(homeZone)+":"+std::to_string(homeTab)+":"+std::to_string(homeTile)+":"+std::to_string(option)+":"+std::to_string(videoIndex)+":"+std::to_string(videoFocus)+":"+std::to_string(consoleIndex)+":"+std::to_string(consoleZone)+":"+std::to_string(menuIndex)+":"+videoMode();};string before=navState();bool tabMotion=(action=="left"||action=="right")&&((hasTabs()&&tabsFocused)||(page=="videos"&&videoFocus==0));
        if(action=="left")move(-1,0);else if(action=="right")move(1,0);else if(action=="up")move(0,-1);else if(action=="down")move(0,1);
        if(before!=navState())uiSound(tabMotion?"tab":(page=="home"&&homeZone==0)||(page=="consoles"&&consoleZone==0&&!tabsFocused)?"scroll":"navigation");
    }
    string keyboard(SDL_Keycode key) {
        switch(key) {
          case SDLK_RETURN:case SDLK_SPACE:return "ok";
          case SDLK_ESCAPE:case SDLK_LCTRL:return "back";
          case SDLK_F1:case SDLK_HOME:return "menu";
          case SDLK_F10:return "fn";case SDLK_F11:case SDLK_POWER:return "power";case SDLK_F12:return "intro";
          case SDLK_VOLUMEUP:return "volume_up";case SDLK_VOLUMEDOWN:return "volume_down";
          case SDLK_LEFT:return "left";case SDLK_RIGHT:return "right";case SDLK_UP:return "up";case SDLK_DOWN:return "down";
          case SDLK_F6:case SDLK_TAB:return "prev";case SDLK_F7:case SDLK_BACKSPACE:return "next";
          case SDLK_F3:return "search";case SDLK_F4:return "sort";
          case SDLK_F9:case SDLK_LSHIFT:return "favorite";
          default:return "";
        }
    }
    void event(const SDL_Event& e) {
        if(e.type==SDL_QUIT){running=false;return;}
        if(e.type==SDL_WINDOWEVENT){dirty=true;return;}
        if(e.type==SDL_KEYUP)discardKeyRepeats=false;
        if(e.type==SDL_KEYDOWN) {
            if(e.key.repeat&&discardKeyRepeats)return;
            if(!e.key.repeat)discardKeyRepeats=false;
            if(device&&(e.key.keysym.sym==SDLK_POWER||e.key.keysym.sym==SDLK_VOLUMEUP||e.key.keysym.sym==SDLK_VOLUMEDOWN))return;
            string mapped=keyboard(e.key.keysym.sym);
            if(mapped=="power"||mapped=="volume_up"||mapped=="volume_down"){button(mapped);return;}
            if(childType=="movie"){button("skip");return;}
            if(searching && e.key.keysym.sym==SDLK_BACKSPACE) {if(!query.empty()){size_t end=query.size()-1;while(end>0&&(query[end]&0xC0)==0x80)--end;query.erase(end);}lastInput=ticks();dirty=true;return;}
            if(!searching || e.key.keysym.sym==SDLK_RETURN || e.key.keysym.sym==SDLK_ESCAPE)button(mapped);
        }
        if(e.type==SDL_TEXTINPUT&&searching){query+=e.text.text;dirty=true;lastInput=ticks();}
        if(e.type==SDL_JOYBUTTONDOWN) {
            static const char* map[]={"ok","back","favorite","favorite","prev","next","search","menu","power"};
            if(e.jbutton.button<9){string action=map[e.jbutton.button];if(action=="power")button(action);else if(childType=="movie")button("skip");else button(action);}
        }
        if(e.type==SDL_JOYHATMOTION){navX=(e.jhat.value&SDL_HAT_LEFT)?-1:(e.jhat.value&SDL_HAT_RIGHT)?1:0;navY=(e.jhat.value&SDL_HAT_UP)?-1:(e.jhat.value&SDL_HAT_DOWN)?1:0;nextNav=0;}
        if(e.type==SDL_JOYAXISMOTION&&e.jaxis.axis<2){int value=std::abs(e.jaxis.value)>18000?(e.jaxis.value<0?-1:1):0;int& target=e.jaxis.axis==0?navX:navY;if(target!=value){target=value;nextNav=0;}}
        if(e.type==SDL_MOUSEWHEEL)button(e.wheel.y>0?"up":"down");
        if(e.type==SDL_MOUSEBUTTONDOWN){if(childType=="movie"){button("skip");return;}if(ticks()<ignoreUntil)return;lastInput=ticks();if(gate.sleeping){wake("any");return;}int x=e.button.x,y=e.button.y;int ww=1280,hh=720;SDL_GetWindowSize(window,&ww,&hh);x=x*1280/ww;y=y*720/hh;
            if(x<190&&y>H-48){button("menu");return;}
            if(menu&&x<280&&y>=116&&y<666){menuIndex=std::clamp((y-116)/55,0,9);accept();return;}
            if(page=="home"){if(y>=130&&y<401){double cardX=x+homeScroll;int idx=cardX>=36&&cardX<476?0:cardX>=496?1+int((cardX-496)/182):-1;if(idx>=0&&idx<int(shown.size())&&cardX>=homeCardX(idx)&&cardX<homeCardX(idx)+homeCardWidth(idx)){homeZone=0;selected=idx;dirty=true;if(e.button.clicks>1)accept();}return;}if(y>=483&&y<523&&x>=254&&x<1030){homeZone=1;homeTab=(x-254)/194;accept();return;}if(y>=545&&y<657&&x>=36&&x<1244){homeZone=2;homeTile=(x-36)/307;accept();return;}}
            if(hasTabs()&&y>=79&&y<117&&x>=104&&x<1243){int wanted=(x-104)/288;tabChange(wanted-tab);tabsFocused=true;dirty=true;return;}
            if(page=="settings"&&y>=166&&y<586){option=std::clamp((option/10)*10+(y-166)/42,0,int(options().size())-1);accept();return;}
            if(page=="detail"&&x>=40&&x<269&&y>=498&&y<558){accept();return;}
            if(page=="videos"&&y>=165&&y<202&&x>=40&&x<1231){int wanted=(x-40)/403;int current=videoMode()=="selected"?1:videoMode()=="single"?2:0;changeVideoMode(wanted-current);videoFocus=0;return;}
            if(page=="videos"&&x<510&&y>=218&&y<596){int idx=(videoIndex/7)*7+(y-218)/54;if(idx<int(movies.size())){videoIndex=idx;videoFocus=1;if(e.button.clicks>1)accept();}dirty=true;return;}
            if(page=="consoles"&&y>=130&&y<348){int idx=int(std::round((x+consoleScroll-184)/300));if(idx>=0&&idx<int(consoles.size())){changeConsole(idx-consoleIndex);tabsFocused=false;consoleZone=0;dirty=true;if(e.button.clicks>1)accept();}return;}
            if(page=="consoles"&&y>=468&&y<642&&x>=49){int column=(x-49)/148,index=(selected/8)*8+column;if(column<8&&index<int(shown.size())){tabsFocused=false;consoleZone=1;selected=index;dirty=true;if(e.button.clicks>1)accept();}return;}
            if(page=="collections"&&x>=42&&x<1258&&y>=150&&y<604){int idx=(selected/8)*8+((y-150)/242)*4+(x-42)/310;int size=int(groups.size());if(idx<size){tabsFocused=false;selected=idx;dirty=true;if(e.button.clicks>1)accept();}return;}
            if((page=="library"||page=="apps"||page=="favorites"||page=="collection"||page=="console"||page=="recents")&&y>=143&&y<630&&x>=49){int col=(x-49)/171,row=(y-143)/262;int idx=(selected/14)*14+row*7+col;if(col<7&&row<2&&idx<int(shown.size())){tabsFocused=false;selected=idx;dirty=true;if(e.button.clicks>1)accept();}}
        }
    }
    bool init() {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_TIMER)!=0||TTF_Init()!=0){std::cerr<<SDL_GetError()<<"\n";return false;}
        IMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);
        window=SDL_CreateWindow("Steam Big Picture - TrimUI",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,W,H,device?SDL_WINDOW_FULLSCREEN:SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
        if(!window)return false;
        render=SDL_CreateRenderer(window,-1,device?SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC:SDL_RENDERER_SOFTWARE);
        if(!render)render=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!render){std::cerr<<SDL_GetError()<<"\n";return false;}
        if(!audio.init(APP+"/sounds"))std::cerr<<"Audio da interface indisponivel: "<<SDL_GetError()<<"\n";
        refreshStatus(true);audio.volume(settings.value("ui_volume",65)*masterVolumePercent/100);
        SDL_RenderSetLogicalSize(render,W,H);SDL_SetRenderDrawBlendMode(render,SDL_BLENDMODE_BLEND);SDL_ShowCursor(device?SDL_DISABLE:SDL_ENABLE);
        for(int i=0;i<SDL_NumJoysticks();++i) {auto* joy=SDL_JoystickOpen(i);if(joy)joysticks.push_back(joy);}
        lastInput=ticks();rebuild();resetHomeAnimation(ticks());trace("start");draw();if(settings.value("intro_on_start",true))movie();return true;
    }
    void loop() {
        uint64_t lastHardware=0,lastClock=0;
        while(running&&!stopRequested) {
            pollChild();
            if(pendingMovieRetry&&ticks()>=movieRetryAt){pendingMovieRetry=false;movie(lastMovie);}
            if(ticks()-lastHardware>150){pollHardware();lastHardware=ticks();}
            refreshStatus();
            uint64_t now=ticks();SDL_Event e{};int timeout=gate.sleeping?150:child>0?75:dirty?int(nextFrame>now?std::min<uint64_t>(nextFrame-now,16):0):40;
            if(SDL_WaitEventTimeout(&e,timeout)) {if(childType!="game")event(e);while(SDL_PollEvent(&e)){if(childType!="game")event(e);}}
            if((navX||navY)&&childType!="game"&&ticks()>=nextNav&&ticks()>=ignoreUntil){bool first=nextNav==0;nextNav=ticks()+(first?320:170);button(navY?(navY<0?"up":"down"):(navX<0?"left":"right"));}
            if(child>0)continue;
            if(!gate.sleeping&&idleSeconds>0&&ticks()-lastInput>=uint64_t(idleSeconds)*1000)sleepScreen();
            if(!gate.sleeping&&page=="home")advanceHomeAnimation(ticks());
            if(!gate.sleeping&&page=="consoles")advanceConsoleAnimation(ticks());
            if(!gate.sleeping&&page=="detail")advanceDetail(ticks());
            uint64_t minute=std::time(nullptr)/60;if(minute!=lastClock){dirty=true;lastClock=minute;}
            if(!gate.sleeping&&ticks()<toastUntil)dirty=true;
            if(dirty&&ticks()>=nextFrame){uint64_t frameStart=ticks();draw();nextFrame=frameStart+16;if(!gate.sleeping&&((page=="home"&&homeAnimating)||(page=="consoles"&&consoleAnimating)||(page=="detail"&&(detailProgress<1||detailClosing))))dirty=true;}
        }
    }
    ~Frontend() {
        if(gate.sleeping)power("on");
        stopMovie();if(child>0){kill(-child,SIGTERM);bool exited=false;for(int i=0;i<50;++i){if(waitpid(child,nullptr,WNOHANG)==child){exited=true;break;}SDL_Delay(20);}if(!exited){kill(-child,SIGKILL);waitpid(child,nullptr,0);}}if(window)persist();
        for(int fd:inputs)close(fd);
        for(auto* joy:joysticks)SDL_JoystickClose(joy);
        for(auto& kv:imageCache){SDL_DestroyTexture(kv.second.texture);SDL_DestroyTexture(kv.second.blur);}for(auto& kv:textCache)SDL_DestroyTexture(kv.second);for(auto& kv:fonts)if(kv.second)TTF_CloseFont(kv.second);
        for(auto& kv:backdropCache){SDL_DestroyTexture(kv.second);}
        for(auto& kv:consoleArtCache){freeConsoleArt(kv.second);}
        audio.shutdown();SDL_DestroyTexture(backgroundShade);SDL_DestroyRenderer(render);SDL_DestroyWindow(window);IMG_Quit();TTF_Quit();SDL_Quit();
    }
};

int main(int argc,char** argv) {
    try {
        signal(SIGTERM,requestStop);signal(SIGINT,requestStop);
        if(argc>1&&string(argv[1])=="--state-test") {
            WakeGate g;g.sleeping=true;g.policy="any";if(!g.accepts("any"))return 1;g.policy="power";if(g.accepts("any")||!g.accepts("power"))return 2;g.fnOff=true;if(g.accepts("any")||g.accepts("power")||!g.accepts("fn_on"))return 3;g.fnOff=false;g.policy="fn";if(g.accepts("any")||!g.accepts("fn_on"))return 4;
            std::cout<<"OK: qualquer botao, Power, Fn e trava do interruptor Fn.\n";return 0;
        }
        Frontend ui;ui.load();
        if(argc>1&&string(argv[1])=="--self-check") {
            std::cout<<J({{"games",ui.games.size()},{"apps",ui.apps.size()},{"consoles",ui.consoles.size()},{"collections",ui.groups.size()},{"movies",ui.movies.size()},{"video_mode",ui.videoMode()},{"video_candidates",ui.moviePool().size()},{"idle_seconds",ui.idleSeconds},{"platform",device?"trimui":"desktop"}}).dump()<<"\n";return 0;
        }
        if(!ui.init())return 1;
        ui.loop();return 0;
    }catch(const std::exception& e){std::cerr<<"Big Picture: "<<e.what()<<"\n";return 1;}
}
