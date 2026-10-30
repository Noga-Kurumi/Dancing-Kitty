#include "beat_engine.hpp"
#include "audio.hpp"
#include "media_watcher.hpp"
#include "bpm_catalog.hpp"
#include <stdexcept>
#include <utility>

namespace kp {
struct BeatEngine::Impl {
    mutable std::mutex lifecycle;
    mutable std::mutex statusMutex;
    EventHandle stopEvent{true},notification,clientNotification;
    Shared shared;
    BeatStatus current;
    bool offline=false;
    std::thread coordinator;
    std::unique_ptr<MusicClassifier> music;
    std::unique_ptr<BeatClock> clock;
    std::unique_ptr<AudioEngine> audio;

    explicit Impl(std::wstring modelDirectory,bool localOnly):offline(localOnly){
        if(modelDirectory.empty())throw std::invalid_argument("Se requiere la carpeta de los modelos de audio");
        shared.modelDirectory=std::move(modelDirectory);
        shared.notificationEvent=notification.value;
        shared.clientNotificationEvent=clientNotification.value;
    }
    void stopCapture(){
        if(clock)clock->setTempo(0);
        audio.reset();clock.reset();music.reset();
        shared.running=false;shared.transportPlaying=false;shared.bMusic=false;shared.trackingState=0;
        shared.trackingConfidence=0;shared.learningProgress=0;
        shared.modelState=0;shared.beatModelState=0;
        shared.beatEnabled=false;shared.catalogBpm=0;shared.effectiveBpm=0;shared.beatInterval=0;shared.beatPhaseLocked=false;shared.nextBeatQpcMs=0;shared.pulseIntervalMs=0;
    }
    bool confirmedMusic()const{return shared.transportPlaying.load()&&shared.running.load()&&shared.bMusic.load()&&shared.classifiedGeneration.load()==shared.generation.load();}
    void fallbackLocal(double rate,const std::wstring& reason){
        if(clock)clock->useLocalTempo(rate);
        std::lock_guard<std::mutex> lock(statusMutex);
        if(!current.catalog.found){current.lookupState=MusicLookupState::EstimatingBpm;current.catalog={};}
        current.catalog.reason=reason;
    }
    void clearTrack(MusicLookupState state,const PlaybackSnapshot& playback){
        if(clock)clock->setTempo(0);
        std::lock_guard<std::mutex> lock(statusMutex);current.lookupState=state;current.playerPlaying=playback.playing||playback.ambiguous;
        current.playerAppId=playback.appId;current.track={};current.catalog={};current.error.clear();
    }
    void run(){
        bool initialized=false;
        try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);initialized=true;
            MediaSessionWatcher watcher(notification.value);
            std::unique_ptr<BpmCatalog> catalog; // Lazy: no network worker until music + metadata.
            uint64_t epoch=0,tokenCounter=0,activeToken=0,failedEpoch=0;
            uint64_t selectedSession=0;double selectedRate=1;
            double pauseDeadline=0;bool capturePaused=false;
            bool metadataAttempted=false,lookupIssued=false,wasMusic=false;
            HANDLE waits[]={stopEvent.value,notification.value};
            SetEvent(notification.value);
            for(;;){
                DWORD timeout=pauseDeadline>0?DWORD(std::max(1.,pauseDeadline-nowMs())):INFINITE;
                DWORD event=WaitForMultipleObjects(2,waits,FALSE,timeout);
                if(event==WAIT_OBJECT_0||event==WAIT_FAILED)break;
                struct ClientUpdate {Shared& state;~ClientUpdate(){state.notifyClient();}} clientUpdate{shared};
                if(event==WAIT_TIMEOUT){
                    // Transport pauses briefly during seeks. After the grace
                    // period stop capture, retaining this track's analysis and
                    // BPM so resume never starts a new tempo lookup/calculation.
                    if(audio){audio->stop();capturePaused=true;}
                    pauseDeadline=0;continue;
                }
                auto playback=watcher.snapshot();
                if(!playback.error.empty()||!playback.playing){
                    bool knownTrack=false;{std::lock_guard<std::mutex> lock(statusMutex);knownTrack=!current.track.title.empty();}
                    if(playback.error.empty()&&!playback.ambiguous&&knownTrack){
                        if(shared.transportPlaying.exchange(false))pauseDeadline=nowMs()+500;
                        wasMusic=false;
                        std::lock_guard<std::mutex> lock(statusMutex);current.playerPlaying=false;current.lookupState=MusicLookupState::WaitingForPlayer;continue;
                    }
                    pauseDeadline=0;capturePaused=false;
                    if(catalog)catalog->cancel();stopCapture();epoch=0;activeToken=++tokenCounter;
                    metadataAttempted=lookupIssued=wasMusic=false;
                    auto state=!playback.error.empty()?MusicLookupState::Error:playback.ambiguous?MusicLookupState::AmbiguousPlayback:MusicLookupState::WaitingForPlayer;
                    clearTrack(state,playback);
                    if(!playback.error.empty()){std::lock_guard<std::mutex> lock(statusMutex);current.error=playback.error;}
                    continue;
                }
                shared.transportPlaying=true;pauseDeadline=0;
                if(epoch!=playback.epoch){
                    bool retained=false;TrackMetadata known;BpmResult knownBpm;
                    {std::lock_guard<std::mutex> lock(statusMutex);known=current.track;knownBpm=current.catalog;}
                    if(audio&&selectedSession==playback.sessionId&&!known.title.empty()){
                        try{
                            auto updated=watcher.metadata(playback,stopEvent.value);
                            if(watcher.snapshot().epoch!=playback.epoch)continue;
                            if(samePlaybackTrack(known,updated)){
                                // Repeated media-property notifications (including
                                // after a seek) do not restart music, models, local
                                // tempo estimation or an already pending lookup.
                                retained=true;epoch=playback.epoch;
                                if(std::abs(selectedRate-playback.playbackRate)>1e-6){
                                    if(shared.localTempoMode.load()&&clock)clock->useLocalTempo(playback.playbackRate);
                                    else if(knownBpm.found&&clock)clock->setTempo(knownBpm.bpm,playback.playbackRate);
                                }
                                if(capturePaused&&audio){audio->start();capturePaused=false;}
                                std::lock_guard<std::mutex> lock(statusMutex);current.track=std::move(updated);current.playerPlaying=true;
                            }
                        }catch(const OperationCanceled&){continue;}catch(...){}
                    }
                    selectedSession=playback.sessionId;selectedRate=playback.playbackRate;
                    if(!retained){
                        epoch=playback.epoch;failedEpoch=0;activeToken=++tokenCounter;
                        if(catalog)catalog->cancel();metadataAttempted=lookupIssued=wasMusic=false;
                        clearTrack(MusicLookupState::CheckingMusic,playback);
                        if(shared.beatModelState.load()<0)stopCapture();
                        if(!audio){
                            {std::lock_guard<std::mutex> lock(shared.textMutex);shared.error.clear();shared.modelError.clear();shared.beatError.clear();}
                            music=std::make_unique<MusicClassifier>(shared);
                            clock=std::make_unique<BeatClock>(shared);
                            audio=std::make_unique<AudioEngine>(shared,*music,clock.get());audio->start();capturePaused=false;
                        }else{music->reset();if(capturePaused){audio->start();capturePaused=false;}}
                    }
                }
                if(failedEpoch==epoch)continue;
                if(shared.modelState.load()<0){
                    if(catalog)catalog->cancel();
                    failedEpoch=epoch;std::wstring reason;
                    {std::lock_guard<std::mutex> lock(shared.textMutex);reason=shared.modelError;}
                    stopCapture();
                    {std::lock_guard<std::mutex> lock(statusMutex);current.lookupState=MusicLookupState::Error;current.error=reason;}
                    continue;
                }
                bool isMusic=confirmedMusic();
                if(!isMusic){
                    bool knownTrack=false;{std::lock_guard<std::mutex> lock(statusMutex);knownTrack=!current.track.title.empty();}
                    if(knownTrack){
                        // A seek can briefly buffer or go silent. Music/audio
                        // freshness already gates output; do not discard this
                        // track's BPM, pending query or analysis configuration.
                        wasMusic=false;
                        std::lock_guard<std::mutex> lock(statusMutex);current.lookupState=MusicLookupState::NonMusic;continue;
                    }
                    if(wasMusic||lookupIssued){if(catalog)catalog->cancel();activeToken=++tokenCounter;metadataAttempted=lookupIssued=false;}
                    wasMusic=false;
                    clearTrack(shared.musicUpdated.load()>0?MusicLookupState::NonMusic:MusicLookupState::CheckingMusic,playback);
                    continue;
                }
                if(!wasMusic&&metadataAttempted){
                    std::lock_guard<std::mutex> lock(statusMutex);
                    if(current.catalog.found)current.lookupState=MusicLookupState::BpmReady;
                    else if(shared.localTempoMode.load())current.lookupState=MusicLookupState::EstimatingBpm;
                    else if(lookupIssued)current.lookupState=MusicLookupState::LookingUpBpm;
                }
                wasMusic=true;
                if(!metadataAttempted){
                    // Estimate from existing audio while metadata and public
                    // providers run. Network latency never delays local learning.
                    if(clock)clock->useLocalTempo(playback.playbackRate);
                    metadataAttempted=true;
                    {std::lock_guard<std::mutex> lock(statusMutex);current.lookupState=MusicLookupState::ReadingMetadata;}
                    TrackMetadata track;
                    try{track=watcher.metadata(playback,stopEvent.value);}
                    catch(const OperationCanceled&){metadataAttempted=false;continue;}
                    catch(const winrt::hresult_error& e){fallbackLocal(playback.playbackRate,std::wstring(e.message()));continue;}
                    catch(const std::exception& e){fallbackLocal(playback.playbackRate,wide(e.what()));continue;}
                    // A late async metadata result cannot authorize a query for
                    // a paused session, an old track or a stale music decision.
                    if(watcher.snapshot().epoch!=epoch||!confirmedMusic()){metadataAttempted=false;continue;}
                    {std::lock_guard<std::mutex> lock(statusMutex);current.track=track;}
                    if(offline){fallbackLocal(playback.playbackRate,L"Modo local sin acceso de red");continue;}
                    if(track.title.empty()||track.artist.empty()){
                        fallbackLocal(playback.playbackRate,L"El reproductor no publico un titulo y artista suficientes");continue;
                    }
                    if(!catalog)catalog=std::make_unique<BpmCatalog>(notification.value,&shared.catalogRequests);
                    activeToken=++tokenCounter;lookupIssued=true;
                    {std::lock_guard<std::mutex> lock(statusMutex);current.lookupState=MusicLookupState::LookingUpBpm;}
                    catalog->request(activeToken,track);
                }
                if(catalog){
                    auto completed=catalog->take();
                    if(completed&&lookupIssued&&completed->token==activeToken&&watcher.snapshot().epoch==epoch&&confirmedMusic()){
                        if(completed->result.found){
                            if(clock)clock->setTempo(completed->result.bpm,playback.playbackRate);
                            std::lock_guard<std::mutex> lock(statusMutex);current.catalog=std::move(completed->result);current.lookupState=MusicLookupState::BpmReady;
                        }else fallbackLocal(playback.playbackRate,completed->result.reason);
                    }
                }
                if(shared.localTempoReady.load()){
                    std::lock_guard<std::mutex> lock(statusMutex);
                    if(shared.localTempoMode.load()&&(!current.catalog.found||current.catalog.estimated)){
                        current.catalog.found=true;current.catalog.estimated=true;current.catalog.bpm=shared.localTempoBpm.load();current.catalog.matchConfidence=shared.localTempoConfidence.load();
                        current.catalog.source=L"Local";current.catalog.matchedTitle=current.track.title;current.catalog.matchedArtist=current.track.artist;current.lookupState=MusicLookupState::BpmReady;
                    }
                }
            }
            if(catalog)catalog->cancel();stopCapture();catalog.reset();
        }catch(const OperationCanceled&){}
        catch(const winrt::hresult_error& e){std::lock_guard<std::mutex> lock(statusMutex);current.lookupState=MusicLookupState::Error;current.error=std::wstring(e.message());}
        catch(const std::exception& e){std::lock_guard<std::mutex> lock(statusMutex);current.lookupState=MusicLookupState::Error;current.error=wide(e.what());}
        stopCapture();
        if(initialized)winrt::uninit_apartment();
    }
};

BeatEngine::BeatEngine(std::wstring modelDirectory,bool offline):impl(std::make_unique<Impl>(std::move(modelDirectory),offline)){}
BeatEngine::~BeatEngine(){stop();}

void BeatEngine::start(){
    std::lock_guard<std::mutex> lock(impl->lifecycle);
    if(impl->coordinator.joinable())return;
    ResetEvent(impl->stopEvent.value);ResetEvent(impl->notification.value);ResetEvent(impl->clientNotification.value);
    {std::lock_guard<std::mutex> statusLock(impl->statusMutex);impl->current={};impl->current.monitoring=true;}
    impl->coordinator=std::thread([this]{impl->run();});
    impl->shared.notifyClient();
}

void BeatEngine::stop(){
    std::lock_guard<std::mutex> lock(impl->lifecycle);
    SetEvent(impl->stopEvent.value);SetEvent(impl->notification.value);
    impl->shared.notifyClient();
    if(impl->coordinator.joinable())impl->coordinator.join();
    std::lock_guard<std::mutex> statusLock(impl->statusMutex);impl->current={};
}

void BeatEngine::relearn(){
    std::lock_guard<std::mutex> lock(impl->lifecycle);
    // Relearn phase in the audio worker while retaining this song's BPM and
    // classification. A manual phase reset does not authorize a new lookup.
    if(!impl->coordinator.joinable())return;
    ++impl->shared.relearn;
    impl->shared.notify();
}

void BeatEngine::setPulseMode(PulseMode mode){
    if(mode!=PulseMode::Auto&&mode!=PulseMode::EveryBeat&&mode!=PulseMode::HalfTime)return;
    impl->shared.pulseMode=int(mode);
    int division=musicalPulseDivision(impl->shared.effectiveBpm.load(),mode);impl->shared.pulseDivision=division;
    impl->shared.pulseIntervalMs=impl->shared.beatInterval.load()*division;
}

BeatStatus BeatEngine::status()const{
    std::lock_guard<std::mutex> lock(impl->statusMutex);
    const auto& s=impl->shared;
    BeatStatus result=impl->current;
    result.capturing=s.running.load();result.bMusic=impl->confirmedMusic();
    result.beatClockEnabled=s.beatEnabled.load()&&result.bMusic;result.beatPhaseLocked=result.beatClockEnabled&&s.beatPhaseLocked.load();result.phaseProvisional=result.beatClockEnabled&&s.phaseProvisional.load();
    result.effectiveBpm=s.effectiveBpm.load();result.playbackRate=s.playbackRate.load();result.beatIntervalMs=s.beatInterval.load();result.nextBeatQpcMs=result.beatPhaseLocked?s.nextBeatQpcMs.load():0;result.tempoRevision=s.tempoGeneration.load();
    result.phaseCorrections=s.phaseCorrections.load();result.phaseAgreement=s.phaseAgreement.load();result.phaseEvidence=s.phaseEvidence.load();
    result.localTempoRefinements=s.localTempoRefinements.load();result.localTempoConfidence=s.localTempoConfidence.load();
    if(s.localTempoMode.load()&&s.localTempoReady.load()&&result.catalog.estimated){result.catalog.bpm=s.localTempoBpm.load();result.catalog.matchConfidence=result.localTempoConfidence;}
    result.pulseMode=PulseMode(s.pulseMode.load());result.pulseDivision=s.pulseDivision.load();result.pulseIntervalMs=s.pulseIntervalMs.load();result.pulseBpm=result.pulseIntervalMs>0?60000/result.pulseIntervalMs:0;
    {std::lock_guard<std::mutex> planLock(s.planMutex);const auto& plan=s.publishedPlan;
        result.pulseScheduleValid=result.beatClockEnabled&&plan.valid&&plan.period>0&&std::abs(plan.period-result.pulseIntervalMs)<.01;
        if(result.pulseScheduleValid){double now=nowMs();double next=plan.origin+(std::floor((now-plan.origin)/plan.period)+1)*plan.period;
            if(next<=plan.expires)result.nextPulseQpcMs=next;else result.pulseScheduleValid=false;}}
    result.captureGeneration=s.generation.load();result.beatModelState=s.beatModelState.load();result.catalogRequests=s.catalogRequests.load();result.classifierWindows=s.classifierWindows.load();
    result.overflows=s.overflows.load();result.discontinuities=s.discontinuities.load();result.audioAgeMs=s.sampleAge.load();result.schedulerErrorMs=s.schedulerErrorMs.load();result.maxSchedulerErrorMs=s.maxSchedulerErrorMs.load();
    result.state=TrackingState(s.trackingState.load());result.confidence=s.trackingConfidence.load();
    result.learningProgress=s.learningProgress.load();result.beatCount=s.pulses.load();result.lastBeatQpcMs=s.lastHit.load();
    std::lock_guard<std::mutex> textLock(impl->shared.textMutex);
    for(const auto* message:{&s.error,&s.modelError,&s.beatError})if(!message->empty()){
        if(!result.error.empty())result.error+=L"; ";result.error+=*message;
    }
    return result;
}
void* BeatEngine::nativeEventHandle()const{return impl->clientNotification.value;}
}
