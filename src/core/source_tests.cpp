#include "bpm_catalog.hpp"
#include <iostream>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    try{
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        check(kp::catalogTimeout(10000,1000)==3000&&kp::catalogTimeout(1234,1200)==34,"A request can exceed the remaining provider budget");
        bool timedOut=false;try{kp::catalogTimeout(1000,1000);}catch(const kp::OperationTimedOut&){timedOut=true;}
        check(timedOut,"Expired provider budget starts another network operation");
        kp::TrackMetadata track{L"Song",L"Artist",L"Album",180};kp::CandidateSelection selected;selected.confidence=.95;
        kp::CatalogCandidate entry;entry.id=L"recording-a";entry.title=L"Song";entry.artist=L"Artist";selected.candidates.push_back(entry);
        auto parse=[](const wchar_t* value){return kp::json::JsonObject::Parse(value);};
        auto result=kp::selectAcousticFeatures(track,selected,parse(LR"({"recording-a":{"0":{"rhythm":{"bpm":118.4},"metadata":{"audio_properties":{"length":180}}}}})"));
        check(result.found&&std::abs(result.bpm-118.4)<.001&&!result.source.empty(),"Valid fallback BPM missing provenance");
        check(!kp::selectAcousticFeatures(track,selected,parse(LR"({"recording-a":{"0":{"rhythm":{"bpm":118.4},"metadata":{"audio_properties":{"length":235}}}}})")).found,"Fallback accepted a different-length recording");
        check(!kp::selectAcousticFeatures(track,selected,parse(LR"({"recording-a":{"0":{"rhythm":{"bpm":0},"metadata":{"audio_properties":{"length":180}}}}})")).found,"Fallback accepted BPM zero");
        check(!kp::selectAcousticFeatures(track,selected,parse(LR"({"recording-a":{"0":{"rhythm":{"bpm":118.4}}}})")).found,"Fallback skipped the audio-duration check");
        check(!kp::selectAcousticFeatures(track,selected,parse(LR"({"recording-a":{"0":{"rhythm":{"bpm":118.4},"metadata":{"audio_properties":{"length":180}}},"1":{"rhythm":{"bpm":135},"metadata":{"audio_properties":{"length":180}}}}})")).found,"Conflicting BPM values silently chosen");
        check(kp::selectAcousticFeatures(track,selected,parse(LR"({"recording-a":{"0":{"rhythm":{"bpm":118.4},"metadata":{"audio_properties":{"length":180}}},"1":{"rhythm":{"bpm":59.2},"metadata":{"audio_properties":{"length":180}}}}})")).found,"Equivalent half-time annotations rejected");
        std::cout<<"PASS fallback source: provenance, duration, missing/zero BPM, conflicts and half-time equivalents\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
