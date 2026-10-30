#pragma once
#include <algorithm>
#include <cmath>
#include "tracking_state.hpp"

namespace kp {
inline int musicalPulseDivision(double effectiveBpm,PulseMode mode){
    if(mode==PulseMode::EveryBeat)return 1;
    if(mode==PulseMode::HalfTime)return 2;
    int division=1;while(effectiveBpm/division>140&&division<16)division*=2;return division;
}
inline int refinedPulseDivision(double bpm,PulseMode mode,int previous){
    if(mode!=PulseMode::Auto)return musicalPulseDivision(bpm,mode);
    int division=std::clamp(previous,1,16);
    while(bpm/division>142&&division<16)division*=2;
    while(division>1&&bpm/(division/2)<138)division/=2;
    return division;
}
inline bool validCatalogBpm(double bpm){return std::isfinite(bpm)&&bpm>=20&&bpm<=400;}
inline double effectiveTempo(double catalogBpm,double playbackRate=1){
    if(!validCatalogBpm(catalogBpm)||!std::isfinite(playbackRate)||playbackRate<.25||playbackRate>4)return 0;
    return catalogBpm*playbackRate;
}
inline double beatIntervalMs(double effectiveBpm){
    return std::isfinite(effectiveBpm)&&effectiveBpm>=5&&effectiveBpm<=1600?60000./effectiveBpm:0;
}
class PlaybackPositionTracker {
    bool initialized=false;
    double previousPosition=0,previousUpdated=0,previousRate=1;
public:
    void reset(){initialized=false;}
    bool observe(double positionMs,double updatedUtcMs,double rate){
        if(!std::isfinite(positionMs)||!std::isfinite(updatedUtcMs)||updatedUtcMs<=0){reset();return false;}
        bool jump=false;
        if(initialized){double elapsed=updatedUtcMs-previousUpdated;
            jump=elapsed<0||std::abs(positionMs-(previousPosition+elapsed*previousRate))>350;}
        previousPosition=positionMs;previousUpdated=updatedUtcMs;previousRate=rate;initialized=true;return jump;
    }
};
}
