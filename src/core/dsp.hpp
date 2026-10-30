#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace kp {
constexpr double pi = 3.14159265358979323846;
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void setup(double fs, double hz, double q, bool high=false) {
        double w=2*pi*hz/fs,c=std::cos(w),s=std::sin(w),alpha=s/(2*q),a0=1+alpha;
        b0=(high ? (1+c) : (1-c))/(2*a0);
        b1=(high ? -(1+c) : (1-c))/a0; b2=b0;
        a1=-2*c/a0; a2=(1-alpha)/a0; z1=z2=0;
    }
    float step(float x) {
        double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y;
        // Avoid denormals after the stream becomes silent.
        if (std::abs(z1)<1e-25) z1=0;
        if (std::abs(z2)<1e-25) z2=0;
        return static_cast<float>(y);
    }
};

// Single producer / single consumer: no locks, allocations or blocking in capture.
template<size_t Capacity> class SampleRing {
    static_assert((Capacity&(Capacity-1))==0);
    std::array<float,Capacity> values{};
    alignas(64) std::atomic<uint64_t> head{0};
    alignas(64) std::atomic<uint64_t> tail{0};
public:
    bool push(float value) {
        auto h=head.load(std::memory_order_relaxed);
        if (h-tail.load(std::memory_order_acquire)>=Capacity) return false;
        values[h&(Capacity-1)]=value;
        head.store(h+1,std::memory_order_release); return true;
    }
    bool pop(float& value) {
        auto t=tail.load(std::memory_order_relaxed);
        if (t==head.load(std::memory_order_acquire)) return false;
        value=values[t&(Capacity-1)]; tail.store(t+1,std::memory_order_release); return true;
    }
    uint64_t discard() {auto h=head.load(std::memory_order_acquire);tail.store(h,std::memory_order_release);return h;}
    uint64_t produced() const{return head.load(std::memory_order_acquire);}
    size_t size() const {return size_t(head.load(std::memory_order_acquire)-tail.load(std::memory_order_acquire));}
};

class Resampler16k {
    Biquad a,b;
    double fs=48000,position=0,next=0;
    float previous=0;
public:
    void reset(double rate) {
        fs=rate; position=next=0; previous=0;
        double cutoff=std::min(6500.0,fs*.40);
        a.setup(fs,cutoff,.541196); b.setup(fs,cutoff,1.306563);
    }
    template<class Sink> void step(float input,Sink sink) {
        float value=b.step(a.step(input));
        while(next<=position) {
            double blend=std::clamp(next-(position-1),0.0,1.0);
            sink(static_cast<float>(previous+(value-previous)*blend));
            next+=fs/16000.0;
        }
        previous=value; position+=1;
    }
};

// Hysteresis belongs to the classifier, never to the kick timing path.
class MusicDecision {
    int positive=0,negative=0;
    bool value=false;
public:
    bool update(float score,bool silent=false) {
        if (silent) { reset(); return false; }
        if(score>=.40f) { ++positive; negative=0; }
        else if(score<.22f) { ++negative; positive=0; }
        else { positive=negative=0; }
        if(positive>=2) value=true;
        if(negative>=2) value=false;
        return value;
    }
    void reset() { positive=negative=0; value=false; }
};

}
