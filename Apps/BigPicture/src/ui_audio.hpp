#pragma once
#include <SDL.h>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

class UiAudio {
    struct Voice { const std::vector<Uint8>* clip=nullptr; size_t offset=0; };
    SDL_AudioDeviceID output=0;
    SDL_AudioSpec spec{};
    std::map<std::string,std::vector<Uint8>> clips;
    std::array<Voice,8> voices{};
    size_t nextVoice=0;
    int gain=SDL_MIX_MAXVOLUME/2;
    bool paused=false;
    static void callback(void* owner,Uint8* stream,int length){static_cast<UiAudio*>(owner)->mix(stream,length);}
public:
    void mix(Uint8* stream,int length){
        std::memset(stream,0,size_t(length));
        for(auto& voice:voices)if(voice.clip){
            size_t remaining=voice.clip->size()-voice.offset;
            int amount=int(std::min(remaining,size_t(length)));
            SDL_MixAudioFormat(stream,voice.clip->data()+voice.offset,AUDIO_S16SYS,Uint32(amount),gain);
            voice.offset+=size_t(amount);if(voice.offset>=voice.clip->size())voice.clip=nullptr;
        }
    }
    bool init(const std::string& directory){
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0)return false;
        SDL_AudioSpec wanted{};wanted.freq=48000;wanted.format=AUDIO_S16SYS;wanted.channels=2;wanted.samples=512;wanted.callback=callback;wanted.userdata=this;
        output=SDL_OpenAudioDevice(nullptr,0,&wanted,&spec,0);
        if(!output)return false;
        for(const char* name:{"scroll","navigation","confirm","back","detail","tab","menu_open","menu_close","launch","toggle_on","toggle_off","volume","error"}){
            SDL_AudioSpec source{};Uint8* bytes=nullptr;Uint32 length=0;
            if(!SDL_LoadWAV((directory+"/"+name+".wav").c_str(),&source,&bytes,&length))continue;
            SDL_AudioCVT convert{};
            int needed=SDL_BuildAudioCVT(&convert,source.format,source.channels,source.freq,spec.format,spec.channels,spec.freq);
            if(needed==0)clips[name]=std::vector<Uint8>(bytes,bytes+length);
            else if(needed>0){
                convert.len=int(length);convert.buf=static_cast<Uint8*>(SDL_malloc(size_t(length)*size_t(convert.len_mult)));
                if(convert.buf){std::memcpy(convert.buf,bytes,length);if(SDL_ConvertAudio(&convert)==0)clips[name]=std::vector<Uint8>(convert.buf,convert.buf+convert.len_cvt);SDL_free(convert.buf);}
            }
            SDL_FreeWAV(bytes);
        }
        SDL_PauseAudioDevice(output,0);return !clips.empty();
    }
    void volume(int percent){if(output)SDL_LockAudioDevice(output);gain=std::clamp(percent,0,100)*SDL_MIX_MAXVOLUME/100;if(output)SDL_UnlockAudioDevice(output);}
    bool play(const std::string& name){
        auto found=clips.find(name);if(!output||paused||found==clips.end())return false;
        SDL_LockAudioDevice(output);voices[nextVoice]={&found->second,0};nextVoice=(nextVoice+1)%voices.size();SDL_UnlockAudioDevice(output);return true;
    }
    void pause(bool value){
        if(value){if(output){SDL_CloseAudioDevice(output);output=0;}for(auto& voice:voices)voice.clip=nullptr;paused=true;}
        else if(paused){SDL_AudioSpec wanted=spec;wanted.callback=callback;wanted.userdata=this;output=SDL_OpenAudioDevice(nullptr,0,&wanted,&spec,0);paused=output==0;if(output)SDL_PauseAudioDevice(output,0);}
    }
    size_t loaded() const {return clips.size();}
    void shutdown(){if(output){SDL_CloseAudioDevice(output);output=0;}}
    ~UiAudio(){shutdown();}
};
