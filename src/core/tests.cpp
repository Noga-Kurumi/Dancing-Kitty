#include "dsp.hpp"
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace kp;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    Resampler16k resampler;resampler.reset(44100);size_t count=0;
    for(int i=0;i<44100;++i)resampler.step(float(std::sin(2*pi*440*i/44100)),[&](float value){require(std::isfinite(value),"Non-finite resampler output");++count;});
    require(count==16000,"Incorrect streaming sample count");
    MusicDecision music;
    require(!music.update(.9f)&&music.update(.9f),"Music hysteresis fails to enter");
    require(music.update(.1f)&&!music.update(.1f),"Music hysteresis fails to leave");
    music.update(.8f);music.update(.8f);require(music.update(.3f),"Ambiguous audio loses the previous decision");
    require(!music.update(0,true),"Silence retains bMusic");
    SampleRing<8> small;
    for(int i=0;i<8;++i)require(small.push(float(i)),"Queue cannot reach capacity");
    require(!small.push(99),"Overflow overwrites unread audio");
    float value=0;for(int i=0;i<8;++i){require(small.pop(value)&&value==i,"Queue lost samples after overflow");}
    require(!small.pop(value),"Empty queue invents a sample");small.push(1);small.push(2);
    auto produced=small.produced();require(small.discard()==produced&&!small.pop(value),"Discard retains old-source samples");
    SampleRing<1024> ring;std::atomic<bool> okay{true};
    std::thread producer([&]{for(int i=0;i<100000;++i)while(!ring.push(float(i)))std::this_thread::yield();});
    for(int i=0;i<100000;++i){float sample;while(!ring.pop(sample))std::this_thread::yield();if(sample!=float(i))okay=false;}
    producer.join();require(okay,"Concurrent audio ordering failed");
    std::cout<<"PASS audio stream: resampling, music gate, silence, queue overflow/discard and concurrent ordering\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
