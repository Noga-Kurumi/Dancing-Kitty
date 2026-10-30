#pragma once
#include "async_wait.hpp"
#include "media_types.hpp"
#include "tempo.hpp"
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <thread>
#include <algorithm>

namespace kp {
namespace media=winrt::Windows::Media::Control;
struct PlaybackSnapshot {
    uint64_t epoch=0,sessionId=0;
    bool playing=false,ambiguous=false;
    double playbackRate=1;
    std::wstring appId,error;
    std::shared_ptr<EventHandle> superseded=std::make_shared<EventHandle>(true);
    media::GlobalSystemMediaTransportControlsSession session{nullptr};
};

class MediaSessionWatcher {
    struct Signals {
        EventHandle stop{true},changed;
        std::atomic<uint64_t> selected{0};
        std::atomic<bool> topology{true},metadata{false},timeline{false};
    };
    struct Binding {
        uint64_t id=0;
        media::GlobalSystemMediaTransportControlsSession session{nullptr};
        media::GlobalSystemMediaTransportControlsSession::PlaybackInfoChanged_revoker playback;
        media::GlobalSystemMediaTransportControlsSession::MediaPropertiesChanged_revoker metadata;
        media::GlobalSystemMediaTransportControlsSession::TimelinePropertiesChanged_revoker timeline;
    };
    std::shared_ptr<Signals> signals=std::make_shared<Signals>();
    HANDLE notify;
    mutable std::mutex mutex;
    PlaybackSnapshot current;
    std::thread worker;
    void publish(PlaybackSnapshot next){
        {std::lock_guard<std::mutex> lock(mutex);SetEvent(current.superseded->value);next.epoch=current.epoch+1;current=std::move(next);}
        SetEvent(notify);
    }
    static void revoke(Binding& binding)noexcept{
        binding.playback.revoke();binding.metadata.revoke();binding.timeline.revoke();
    }
    void run(){
        bool initialized=false;
        media::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
        media::GlobalSystemMediaTransportControlsSessionManager::SessionsChanged_revoker sessionsToken;
        media::GlobalSystemMediaTransportControlsSessionManager::CurrentSessionChanged_revoker currentToken;
        std::vector<Binding> bindings;
        try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);initialized=true;
            manager=awaitResult(media::GlobalSystemMediaTransportControlsSessionManager::RequestAsync(),signals->stop.value,nullptr);
            std::weak_ptr<Signals> weak=signals;
            sessionsToken=manager.SessionsChanged(winrt::auto_revoke,[weak](const auto&,const auto&){if(auto s=weak.lock()){s->topology=true;SetEvent(s->changed.value);}});
            currentToken=manager.CurrentSessionChanged(winrt::auto_revoke,[weak](const auto&,const auto&){if(auto s=weak.lock())SetEvent(s->changed.value);});
            uint64_t nextId=1;
            PlaybackPositionTracker position;
            HANDLE waits[]={signals->stop.value,signals->changed.value};
            SetEvent(signals->changed.value);
            while(WaitForMultipleObjects(2,waits,FALSE,INFINITE)==WAIT_OBJECT_0+1){
                if(signals->topology.exchange(false)){
                    std::vector<Binding> rebuilt;
                    for(const auto& session:manager.GetSessions()){
                        auto old=std::find_if(bindings.begin(),bindings.end(),[&](const Binding& b){return b.session==session;});
                        uint64_t id=old==bindings.end()?nextId++:old->id;
                        Binding binding;binding.id=id;binding.session=session;
                        binding.playback=session.PlaybackInfoChanged(winrt::auto_revoke,[weak](const auto&,const auto&){if(auto s=weak.lock())SetEvent(s->changed.value);});
                        binding.metadata=session.MediaPropertiesChanged(winrt::auto_revoke,[weak,id](const auto&,const auto&){if(auto s=weak.lock())if(s->selected.load()==id){s->metadata=true;SetEvent(s->changed.value);}});
                        binding.timeline=session.TimelinePropertiesChanged(winrt::auto_revoke,[weak,id](const auto&,const auto&){if(auto s=weak.lock())if(s->selected.load()==id){s->timeline=true;SetEvent(s->changed.value);}});
                        rebuilt.push_back(std::move(binding));
                    }
                    for(auto& old:bindings)revoke(old);
                    bindings=std::move(rebuilt);
                }
                PlaybackSnapshot next;uint64_t selected=0;size_t playingCount=0;
                for(const auto& binding:bindings){
                    try{
                        auto info=binding.session.GetPlaybackInfo();
                        if(info.PlaybackStatus()!=media::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)continue;
                        ++playingCount;selected=binding.id;next.session=binding.session;next.appId=std::wstring(binding.session.SourceAppUserModelId());
                        if(auto rate=info.PlaybackRate())if(std::isfinite(rate.Value())&&rate.Value()>0)next.playbackRate=rate.Value();
                    }catch(...){}
                }
                next.ambiguous=playingCount>1;next.playing=playingCount==1;
                if(!next.playing){next.session=nullptr;next.appId.clear();selected=0;}
                next.sessionId=selected;
                bool metadataChanged=signals->metadata.exchange(false);
                bool rateChanged=false;{std::lock_guard<std::mutex> lock(mutex);rateChanged=std::abs(next.playbackRate-current.playbackRate)>1e-6;}
                bool changed=metadataChanged||selected!=signals->selected.load()||rateChanged;
                bool timelineChanged=signals->timeline.exchange(false);
                if(changed)position.reset();
                if(next.playing&&(changed||timelineChanged)){
                    try{
                        auto timeline=next.session.GetTimelineProperties();
                        double pos=double(timeline.Position().count())/10000.;
                        double updated=double(timeline.LastUpdatedTime().time_since_epoch().count())/10000.;
                        // Position jumps are not track changes. Keep the active
                        // identity/BPM and let the audio window relocate phase.
                        position.observe(pos,updated,next.playbackRate);
                    }catch(...){position.reset();}
                }else if(!next.playing)position.reset();
                {std::lock_guard<std::mutex> lock(mutex);changed=changed||next.playing!=current.playing||next.ambiguous!=current.ambiguous||!current.error.empty();}
                signals->selected=selected;
                if(changed)publish(std::move(next));
            }
        }catch(const OperationCanceled&){}
        catch(const winrt::hresult_error& e){PlaybackSnapshot failure;failure.error=std::wstring(e.message());publish(std::move(failure));}
        catch(const std::exception& e){PlaybackSnapshot failure;failure.error=wide(e.what());publish(std::move(failure));}
        for(auto& binding:bindings)revoke(binding);
        sessionsToken.revoke();currentToken.revoke();
        bindings.clear();manager=nullptr;
        if(initialized)winrt::uninit_apartment();
    }
public:
    explicit MediaSessionWatcher(HANDLE event):notify(event),worker([this]{run();}){}
    ~MediaSessionWatcher(){SetEvent(signals->stop.value);if(worker.joinable())worker.join();}
    PlaybackSnapshot snapshot()const{std::lock_guard<std::mutex> lock(mutex);return current;}
    TrackMetadata metadata(const PlaybackSnapshot& snapshot,HANDLE stop)const{
        if(!snapshot.playing||!snapshot.session)throw OperationCanceled();
        auto properties=awaitResult(snapshot.session.TryGetMediaPropertiesAsync(),stop,snapshot.superseded->value,3000);
        TrackMetadata result;result.title=std::wstring(properties.Title());result.artist=std::wstring(properties.Artist());result.album=std::wstring(properties.AlbumTitle());
        try{
            auto timeline=snapshot.session.GetTimelineProperties();
            result.durationSeconds=std::max(0.,double((timeline.EndTime()-timeline.StartTime()).count())/10000000.);
        }catch(...){}
        return result;
    }
};
}
