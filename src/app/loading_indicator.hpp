#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace catpulse {
class LoadingClock {
    double origin=0;
    bool enabled=false;
    int frame=-1;
public:
    int update(double now,bool pending){
        if(pending&&!enabled)origin=now;
        enabled=pending;frame=enabled?int(std::floor(std::max(0.,now-origin)/100))%8:-1;return frame;
    }
    double nextFrameTime(double now)const{return enabled?origin+(std::floor(std::max(0.,now-origin)/100)+1)*100:std::numeric_limits<double>::infinity();}
};
// Temporarily composite eight antialiased gray dots into the cached sprite.
// Back up and restore only the forehead rectangle, including its alpha.
class LoadingOverlay {
    uint8_t* pixels;int side,left,top,right,bottom;
    std::vector<uint8_t>& saved;
public:
    LoadingOverlay(uint8_t* target,int size,int phase,std::vector<uint8_t>& backup):pixels(target),side(size),saved(backup){
        double cx=side*.505,cy=side*.285,ring=side*.061;
        constexpr double outline=1.; // One rendered pixel, independent of cat scale.
        double margin=ring+side*.016+outline+1;
        left=std::max(0,int(std::floor(cx-margin)));right=std::min(side,int(std::ceil(cx+margin)));
        top=std::max(0,int(std::floor(cy-margin)));bottom=std::min(side,int(std::ceil(cy+margin)));
        saved.resize(size_t(right-left)*(bottom-top)*4);
        for(int y=top;y<bottom;++y)std::copy_n(pixels+(size_t(y)*side+left)*4,(right-left)*4,saved.data()+size_t(y-top)*(right-left)*4);
        constexpr double pi=3.14159265358979323846;
        for(int dot=0;dot<8;++dot){
            int age=(phase-dot+8)%8;if(age==7)continue;
            double angle=dot*pi/4-pi/2,x=cx+ring*std::cos(angle),y=cy+ring*std::sin(angle);
            double radius=side*.016*(1-age*.115),opacity=1-age*.11;int gray=42+age*22;
            for(int py=std::max(top,int(std::floor(y-radius-outline-1)));py<std::min(bottom,int(std::ceil(y+radius+outline+1)));++py)
                for(int px=std::max(left,int(std::floor(x-radius-outline-1)));px<std::min(right,int(std::ceil(x+radius+outline+1)));++px){
                    double distance=std::hypot(px+.5-x,py+.5-y);
                    double coverage=std::clamp(radius+outline+.5-distance,0.,1.);
                    unsigned alpha=unsigned(std::lround(255*opacity*coverage));if(!alpha)continue;
                    double core=std::clamp(radius+.5-distance,0.,1.);
                    int shade=int(std::lround(255+(gray-255)*core/coverage));
                    auto p=pixels+(size_t(py)*side+px)*4;
                    for(int channel=0;channel<3;++channel)p[channel]=uint8_t((shade*alpha+p[channel]*(255-alpha)+127)/255);
                    p[3]=uint8_t(alpha+(p[3]*(255-alpha)+127)/255);
                }
        }
    }
    ~LoadingOverlay(){for(int y=top;y<bottom;++y)std::copy_n(saved.data()+size_t(y-top)*(right-left)*4,(right-left)*4,pixels+(size_t(y)*side+left)*4);}
    LoadingOverlay(const LoadingOverlay&)=delete;
    LoadingOverlay& operator=(const LoadingOverlay&)=delete;
};
}
