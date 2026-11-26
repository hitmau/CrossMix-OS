#pragma once
#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

struct ConsoleArt {
    SDL_Texture* image=nullptr;
    SDL_Texture* outline=nullptr;
    SDL_Texture* shadow=nullptr;
    int w=1,h=1;
    size_t solidPixels=0,outlinePixels=0;
};
inline void freeConsoleArt(ConsoleArt& art){SDL_DestroyTexture(art.image);SDL_DestroyTexture(art.outline);SDL_DestroyTexture(art.shadow);}
inline ConsoleArt makeConsoleArt(SDL_Renderer* renderer,const std::string& path){
    ConsoleArt result;
    SDL_Surface* loaded=IMG_Load(path.c_str());if(!loaded)return result;
    SDL_Surface* source=SDL_ConvertSurfaceFormat(loaded,SDL_PIXELFORMAT_RGBA32,0);SDL_FreeSurface(loaded);if(!source)return result;
    int left=source->w,top=source->h,right=-1,bottom=-1;
    for(int y=0;y<source->h;++y){const auto* row=static_cast<Uint8*>(source->pixels)+y*source->pitch;for(int x=0;x<source->w;++x)if(row[x*4+3]>32){left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}}
    if(right<left||bottom<top){SDL_FreeSurface(source);return result;}
    SDL_Rect crop{left,top,right-left+1,bottom-top+1};
    double scale=std::min(260.0/crop.w,180.0/crop.h);int width=std::max(1,int(std::round(crop.w*scale))),height=std::max(1,int(std::round(crop.h*scale)));
    const int pad=12;result.w=width+pad*2;result.h=height+pad*2;
    auto* canvas=SDL_CreateRGBSurfaceWithFormat(0,result.w,result.h,32,SDL_PIXELFORMAT_RGBA32);
    auto* stroke=SDL_CreateRGBSurfaceWithFormat(0,result.w,result.h,32,SDL_PIXELFORMAT_RGBA32);
    auto* shadow=SDL_CreateRGBSurfaceWithFormat(0,result.w,result.h,32,SDL_PIXELFORMAT_RGBA32);
    if(!canvas||!stroke||!shadow){SDL_FreeSurface(source);SDL_FreeSurface(canvas);SDL_FreeSurface(stroke);SDL_FreeSurface(shadow);return result;}
    SDL_FillRect(canvas,nullptr,0);SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);SDL_Rect dst{pad,pad,width,height};SDL_BlitScaled(source,&crop,canvas,&dst);SDL_FreeSurface(source);
    int w=result.w,h=result.h;std::vector<float> mask(size_t(w*h)),horizontal(size_t(w*h)),blur(size_t(w*h));
    for(int y=0;y<h;++y){const auto* row=static_cast<Uint8*>(canvas->pixels)+y*canvas->pitch;for(int x=0;x<w;++x){mask[size_t(y*w+x)]=row[x*4+3]>=64?255.0f:0.0f;if(mask[size_t(y*w+x)])++result.solidPixels;}}
    const int radius=5;
    for(int y=0;y<h;++y){float sum=0;for(int x=-radius;x<w+radius;++x){int add=x+radius,remove=x-radius-1;if(add>=0&&add<w)sum+=mask[size_t(y*w+add)];if(remove>=0&&remove<w)sum-=mask[size_t(y*w+remove)];if(x>=0&&x<w)horizontal[size_t(y*w+x)]=sum/(radius*2+1);}}
    for(int x=0;x<w;++x){float sum=0;for(int y=-radius;y<h+radius;++y){int add=y+radius,remove=y-radius-1;if(add>=0&&add<h)sum+=horizontal[size_t(add*w+x)];if(remove>=0&&remove<h)sum-=horizontal[size_t(remove*w+x)];if(y>=0&&y<h)blur[size_t(y*w+x)]=sum/(radius*2+1);}}
    for(int y=0;y<h;++y){auto* edge=static_cast<Uint8*>(stroke->pixels)+y*stroke->pitch;auto* shade=static_cast<Uint8*>(shadow->pixels)+y*shadow->pitch;
        for(int x=0;x<w;++x){bool outside=mask[size_t(y*w+x)]==0,near=false;if(outside)for(int dy=-3;dy<=3&&!near;++dy)for(int dx=-3;dx<=3;++dx){if(dx*dx+dy*dy>9)continue;int xx=x+dx,yy=y+dy;if(xx>=0&&xx<w&&yy>=0&&yy<h&&mask[size_t(yy*w+xx)]>0){near=true;break;}}
            edge[x*4]=edge[x*4+1]=edge[x*4+2]=255;edge[x*4+3]=near?255:0;if(near)++result.outlinePixels;
            shade[x*4]=shade[x*4+1]=shade[x*4+2]=0;shade[x*4+3]=Uint8(std::clamp(blur[size_t(y*w+x)]*.72f,0.0f,255.0f));
        }
    }
    result.image=SDL_CreateTextureFromSurface(renderer,canvas);result.outline=SDL_CreateTextureFromSurface(renderer,stroke);result.shadow=SDL_CreateTextureFromSurface(renderer,shadow);
    SDL_FreeSurface(canvas);SDL_FreeSurface(stroke);SDL_FreeSurface(shadow);
    for(auto* texture:{result.image,result.outline,result.shadow})if(texture)SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    return result;
}
