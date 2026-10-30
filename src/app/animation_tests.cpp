#include "animation_clock.hpp"
#include "resize_geometry.hpp"
#include "i18n.hpp"
#include "loading_indicator.hpp"
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    using namespace catpulse;
    check(cornerAtBounds({0,0,260,260},2,2)==Corner::TopLeft&&cornerAtBounds({0,0,260,260},258,2)==Corner::TopRight,"Top resize corners are not hit-testable");
    check(cornerAtBounds({0,0,260,260},2,258)==Corner::BottomLeft&&cornerAtBounds({0,0,260,260},258,258)==Corner::BottomRight,"Bottom resize corners are not hit-testable");
    check(cornerAtBounds({0,0,260,260},130,130)==Corner::None,"Dragging the cat resizes it");
    auto grown=resizedVisible({100,200,260},{},Corner::TopLeft,-100,-80);
    check(grown.size==360&&grown.x+grown.size==360&&grown.y+grown.size==460,"Resize moves the opposite corner");
    auto minimum=resizedVisible({100,200,260},{},Corner::BottomRight,-900,-900);
    check(minimum.size==96&&minimum.x==100&&minimum.y==200,"Resize minimum or anchoring is wrong");
    check(resizedVisible({0,0,260},{},Corner::BottomRight,9000,9000).size==1000,"Resize allocation is not bounded");
    NormalizedBounds visible{72./374,57./374,313./374,369./374};auto bounds=scaledBounds(visible,260);
    check(cornerAtBounds(bounds,0,0)==Corner::None&&cornerAtBounds(bounds,bounds.left,bounds.top)==Corner::TopLeft,"Resize extends outside frame 1 visible bounds");
    auto changed=resizedVisible({100,200,260},visible,Corner::TopLeft,-70,-80);auto changedBounds=scaledBounds(visible,changed.size);
    check(changed.x+changedBounds.right==100+bounds.right&&changed.y+changedBounds.bottom==200+bounds.bottom,"Visible opposite corner is not anchored");
    check(std::wstring(matchingLanguage(L"es-AR")->close)==L"Cerrar"&&matchingLanguage(L"zh-Hant-TW")==&translations[11],"Windows language variants select the wrong menu");
    check(matchingLanguage(L"ar-SA")->rtl&&matchingLanguage(L"xx-XX")==nullptr,"Locale fallback or RTL is wrong");
    for(const auto& language:translations)check(wcslen(language.startup)>0&&wcslen(language.close)>0,"A supported language has empty menu text");
    AnimationClock initial(100);
    check(initial.update(100,{})==0,"Launch does not start the initial loop");
    check(initial.update(300,{})==5,"Idle stroke has wrong frame timing");
    check(initial.update(460,{})==9&&initial.stopped(),"Idle stroke does not stop on frame 10");
    check(initial.update(100000,{})==9&&!std::isfinite(initial.nextFrameTime()),"Idle loop wakes or repeats");
    AnimationClock clock(0);Rhythm beat{true,1000,500,1500};
    check(clock.update(1000,beat)==0,"First frame misses the clap");
    check(clock.update(1039,beat)==0&&clock.update(1040,beat)==1,"Standard frame rate changes with BPM");
    check(clock.update(1360,beat)==9&&clock.stopped(),"Frame 10 does not wait for the next clap");
    // A prediction without a new emitted pulse cannot invent another loop.
    beat.nextPulse=2000;check(clock.update(1500,beat)==9,"Predicted deadline invents frame 1");
    beat.pulse=1500;check(clock.update(1500,beat)==0,"New clap does not restart at frame 1");
    clock.update(1630,beat);beat.music=false;
    check(clock.update(1640,beat)>=2,"Stopping music resets the animation");
    check(clock.update(1950,beat)==9&&clock.stopped(),"Music pause does not finish the loop");
    beat.music=true;check(clock.update(2500,beat)==9,"Old pulse restarts after music returns");
    beat={true,3000,750,3750};check(clock.update(3000,beat)==0,"Music resume misses its first new clap");
    check(clock.update(3200,beat)==5,"Slow music slowed down the standard stroke");
    check(clock.update(3375,beat)==9&&clock.stopped(),"A slow song creates a halfway animation");
    check(clock.update(3740,beat)==9&&!std::isfinite(clock.nextFrameTime()),"Waiting for a slow beat wakes the UI");
    beat={true,4000,250,4250};check(clock.update(4000,beat)==0,"Fast beat misses frame 1");
    check(clock.update(4025,beat)==1&&clock.update(4225,beat)==9,"Fast stroke cannot fit before the beat");
    beat={true,5000,500,5500};check(clock.update(5000,beat)==0,"Normal stroke cannot resume after fast music");
    check(clock.update(5080,beat)==2,"Fast song permanently changed the normal speed");
    beat.nextPulse=5300;check(clock.update(5100,beat)>=3,"An earlier beat does not accelerate the current stroke");
    beat.nextPulse=5700;check(clock.update(5200,beat)>=6,"A schedule refinement slows or reverses the current stroke");
    for(int i=1;i<=5000;++i){double stamp=4000+i*471.6981132075;Rhythm r{true,stamp,471.6981132075,stamp+471.6981132075};
        if(stamp<6000)continue;
        check(clock.update(stamp,r)==0,"Long-running animation accumulates drift");
        check(clock.update(stamp+430,r)==9,"Loop cannot finish before a fractional beat");}
    LoadingClock loader;
    check(loader.update(0,false)==-1&&!std::isfinite(loader.nextFrameTime(0)),"Idle loader wakes the UI");
    check(loader.update(123,true)==0&&loader.update(822,true)==6&&loader.update(923,true)==0,"Loader does not use eight 100 ms frames");
    check(loader.update(924,false)==-1&&!std::isfinite(loader.nextFrameTime(924)),"Resolved BPM leaves the loader active");
    for(int side:{96,147,374,1000}){
        std::vector<uint8_t> image(size_t(side)*side*4,120),backup;for(size_t i=3;i<image.size();i+=4)image[i]=255;auto original=image;
        for(int phase=0;phase<8;++phase){
            {LoadingOverlay overlay(image.data(),side,phase,backup);check(image!=original,"Loading indicator has no visible pixels");
                check(image[0]==original[0]&&image.back()==original.back(),"Loading indicator draws outside the forehead");}
            check(image==original,"Loading indicator permanently altered a cached PNG frame");
        }
    }
    std::cout<<"PASS animation: normal/fast strokes, hold frame 10, no doubled slow beats, phase refinements, loader timing/compositing and long-run timing\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
