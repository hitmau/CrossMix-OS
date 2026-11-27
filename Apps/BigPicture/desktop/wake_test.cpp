#define main bigpicture_program_main
#include "../src/main.cpp"
#undef main

int main() {
    Frontend ui; ui.load();
    ui.settings["intro_on_start"]=false;
    ui.settings["intro_on_wake"]=true;
    if(!ui.init() || ui.movies.empty())return 1;
    ui.sleepScreen();
    if(!ui.gate.sleeping || !ui.moviePrepared || ui.child<=0)return 2;
    const auto deadline=ticks()+10000;
    while(!ui.movieReady && ui.child>0 && ticks()<deadline) {
        ui.pollChild();ui.pollPreparedMovie();SDL_Delay(25);
    }
    if(!ui.movieReady)return 3;
    ui.wake("power",true);
    if(ui.gate.sleeping || ui.wakeWaiting || ui.moviePrepared || ui.child<=0)return 4;
    ui.stopMovie();
    ui.settings["intro_on_wake"]=false;
    ui.sleepScreen();ui.wake("any");
    if(ui.gate.sleeping || ui.wakeWaiting || ui.child>0)return 5;
    std::cout<<"OK: video prepared and resumed; disabled intro returns to interface.\n";
}
