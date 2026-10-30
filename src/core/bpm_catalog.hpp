#pragma once
#include "async_wait.hpp"
#include "track_match.hpp"
#include "tempo.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <optional>
#include <thread>
#include <map>

namespace kp {
namespace json=winrt::Windows::Data::Json;
namespace http=winrt::Windows::Web::Http;
namespace filters=winrt::Windows::Web::Http::Filters;
struct BpmLookupCompletion {uint64_t token=0;BpmResult result;};
inline DWORD catalogTimeout(double deadline,double now){
    if(deadline<=now)throw OperationTimedOut();
    return DWORD(std::clamp(std::ceil(deadline-now),1.,3000.));
}

inline BpmResult selectAcousticFeatures(const TrackMetadata& track,const CandidateSelection& selected,const json::JsonObject& features){
    BpmResult result;double chosen=0;
        for(const auto& entry:selected.candidates){
            auto offsets=features.GetNamedObject(entry.id,json::JsonObject{});
            for(const auto& offset:offsets){
                if(offset.Value().ValueType()!=json::JsonValueType::Object)continue;auto data=offset.Value().GetObject();
                double bpm=data.GetNamedObject(L"rhythm",json::JsonObject{}).GetNamedNumber(L"bpm",0);if(!validCatalogBpm(bpm))continue;
                auto metadata=data.GetNamedObject(L"metadata",json::JsonObject{}).GetNamedObject(L"audio_properties",json::JsonObject{});
                double duration=metadata.GetNamedNumber(L"length",0);
                if(track.durationSeconds>0&&(duration<=0||std::abs(duration-track.durationSeconds)>std::max(3.,track.durationSeconds*.015)))continue;
                if(chosen>0){double normalized=bpm;while(normalized<chosen*.70)normalized*=2;while(normalized>chosen*1.40)normalized/=2;
                    if(std::abs(normalized-chosen)>chosen*.03){result={};result.reason=L"AcousticBrainz contiene valores incompatibles para la grabacion";return result;}}
                if(chosen==0){chosen=bpm;result.found=true;result.bpm=bpm;result.matchConfidence=selected.confidence;result.source=L"AcousticBrainz (MusicBrainz)";
                    result.sourceUrl=L"https://acousticbrainz.org/"+entry.id;result.matchedTitle=entry.title;result.matchedArtist=entry.artist;}
            }
        }
        if(!result.found)result.reason=L"AcousticBrainz no tiene BPM y duracion compatibles para la grabacion";return result;
}

class BpmCatalog {
    struct Job {uint64_t token;TrackMetadata track;std::shared_ptr<EventHandle> canceled=std::make_shared<EventHandle>(true);double deadline=0;};
    struct CacheEntry {BpmResult result;double expires=0;};
    EventHandle stopEvent{true},workEvent;
    HANDLE notify;std::mutex mutex;
    std::atomic<uint64_t>* requestCounter=nullptr;
    std::optional<Job> pending;
    std::shared_ptr<EventHandle> active;
    std::optional<BpmLookupCompletion> completed;
    std::thread worker;
    std::map<std::wstring,CacheEntry> cache;
    std::wstring fatalError;
    std::map<std::wstring,double> blockedUntil,lastRequest;

    static std::wstring encodeQuery(const std::wstring& input){
        int n=WideCharToMultiByte(CP_UTF8,0,input.data(),int(input.size()),nullptr,0,nullptr,nullptr);
        std::string bytes(size_t(n),'\0');if(n)WideCharToMultiByte(CP_UTF8,0,input.data(),int(input.size()),bytes.data(),n,nullptr,nullptr);
        static constexpr wchar_t hex[]=L"0123456789ABCDEF";std::wstring result;
        for(unsigned char c:bytes){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')result+=wchar_t(c);else{result+=L'%';result+=hex[c>>4];result+=hex[c&15];}}
        return result;
    }
    static CatalogCandidate candidate(const json::JsonObject& object){
        CatalogCandidate value;
        auto number=object.GetNamedNumber(L"id",0);if(number>0&&number<9e15)value.id=std::to_wstring(uint64_t(number));
        value.title=std::wstring(object.GetNamedString(L"title",L""));value.shortTitle=std::wstring(object.GetNamedString(L"title_short",L""));
        value.version=std::wstring(object.GetNamedString(L"title_version",L""));value.isrc=std::wstring(object.GetNamedString(L"isrc",L""));value.duration=object.GetNamedNumber(L"duration",0);
        value.artist=std::wstring(object.GetNamedObject(L"artist",json::JsonObject{}).GetNamedString(L"name",L""));
        value.album=std::wstring(object.GetNamedObject(L"album",json::JsonObject{}).GetNamedString(L"title",L""));return value;
    }
    json::JsonObject get(http::HttpClient& client,const std::wstring& path,const Job& job,const std::wstring& host=L"https://api.deezer.com"){
        if(host!=L"https://api.deezer.com"&&host!=L"https://musicbrainz.org"&&host!=L"https://acousticbrainz.org")throw std::runtime_error("Proveedor no permitido");
        if(nowMs()<blockedUntil[host])throw std::runtime_error("El catalogo esta temporalmente limitado; no se reintentara en bucle");
        if(WaitForSingleObject(job.canceled->value,0)==WAIT_OBJECT_0)throw OperationCanceled();
        double minimum=host==L"https://musicbrainz.org"?1100:250;
        auto delay=DWORD(std::max(0.,minimum-(nowMs()-lastRequest[host])));
        HANDLE waits[]={stopEvent.value,job.canceled->value};if(delay&&WaitForMultipleObjects(2,waits,FALSE,std::min(delay,catalogTimeout(job.deadline,nowMs())))!=WAIT_TIMEOUT)throw OperationCanceled();
        auto budget=[&](){return catalogTimeout(job.deadline,nowMs());};
        budget();
        lastRequest[host]=nowMs();
        if(requestCounter)++*requestCounter;
        auto timeout=budget();
        auto response=awaitResult(client.GetAsync(winrt::Windows::Foundation::Uri(host+path),http::HttpCompletionOption::ResponseHeadersRead),stopEvent.value,job.canceled->value,timeout);
        auto status=uint32_t(response.StatusCode());
        if(status==429){
            double seconds=60;winrt::hstring header;
            if(response.Headers().HasKey(L"Retry-After")){header=response.Headers().Lookup(L"Retry-After");try{seconds=std::max(60.,std::stod(std::wstring(header)));}catch(...){}}
            blockedUntil[host]=nowMs()+std::min(seconds,86400.)*1000;
            throw std::runtime_error("El catalogo ha limitado temporalmente las consultas");
        }
        if(status!=200)throw std::runtime_error("El catalogo no esta disponible para una consulta anonima (HTTP "+std::to_string(status)+")");
        auto contentType=response.Content().Headers().ContentType();
        if(!contentType||contentType.MediaType()!=L"application/json")throw std::runtime_error("El catalogo no devolvio una respuesta JSON");
        timeout=budget();auto stream=awaitResult(response.Content().ReadAsInputStreamAsync(),stopEvent.value,job.canceled->value,timeout);
        std::string bytes;
        for(;;){
            timeout=budget();auto buffer=awaitResult(stream.ReadAsync(winrt::Windows::Storage::Streams::Buffer(4096),4096,winrt::Windows::Storage::Streams::InputStreamOptions::Partial),stopEvent.value,job.canceled->value,timeout);
            if(!buffer.Length())break;
            if(bytes.size()+buffer.Length()>512*1024)throw std::runtime_error("La respuesta del catalogo excede el limite de datos");
            auto reader=winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
            std::vector<uint8_t> part(buffer.Length());reader.ReadBytes(part);bytes.append(reinterpret_cast<const char*>(part.data()),part.size());
        }
        json::JsonObject object{nullptr};
        if(!json::JsonObject::TryParse(winrt::to_hstring(bytes),object))throw std::runtime_error("La respuesta del catalogo no es JSON valido");
        if(object.HasKey(L"error"))throw std::runtime_error("El catalogo informo un error; no se asigno un BPM");
        return object;
    }
    BpmResult lookupDeezer(http::HttpClient& client,const Job& job,std::vector<CatalogCandidate>& matched){
        BpmResult result;
        // Anonymous public catalogue only. No audio, account tokens, cookies or
        // player identifiers are sent; previews are neither read nor downloaded.
        auto search=get(client,L"/search?q="+encodeQuery(job.track.artist+L" "+searchTitle(job.track.title))+L"&limit=12",job);
        std::vector<CatalogCandidate> candidates;
        for(const auto& item:search.GetNamedArray(L"data",json::JsonArray{})){
            if(item.ValueType()!=json::JsonValueType::Object)continue;
            auto entry=candidate(item.GetObject());if(!entry.id.empty())candidates.push_back(std::move(entry));
        }
        auto selection=selectRecording(job.track,candidates);
        result.reason=selection.reason;
        for(const auto& entry:selection.candidates){
            auto detail=get(client,L"/track/"+entry.id,job);auto checked=candidate(detail);
            if(matchTrack(job.track,checked)==0)continue;
            matched.push_back(checked);
            double bpm=detail.GetNamedNumber(L"bpm",0);
            if(!validCatalogBpm(bpm))continue;
            result.found=true;result.bpm=bpm;result.matchConfidence=selection.confidence;result.source=L"Deezer public catalogue";
            result.sourceUrl=L"https://www.deezer.com/track/"+entry.id;result.matchedTitle=checked.title;result.matchedArtist=checked.artist;result.reason.clear();break;
        }
        if(!result.found&&result.reason.empty())result.reason=L"La grabacion encontrada no publica un BPM valido";
        return result;
    }
    BpmResult lookupBrainz(http::HttpClient& client,const Job& job,const std::vector<CatalogCandidate>& verified){
        std::wstring isrc;for(const auto& entry:verified)if(!entry.isrc.empty()){isrc=entry.isrc;break;}
        auto quoted=[](const std::wstring& value){std::wstring out=L"\"";for(wchar_t c:value){if(c==L'"'||c==L'\\')out+=L'\\';out+=c;}return out+L"\"";};
        std::wstring query=isrc.empty()?L"recording:"+quoted(searchTitle(job.track.title))+L" AND artist:"+quoted(job.track.artist):L"isrc:"+quoted(isrc);
        auto document=get(client,L"/ws/2/recording/?query="+encodeQuery(query)+L"&fmt=json&limit=8",job,L"https://musicbrainz.org");
        std::vector<CatalogCandidate> candidates;
        for(const auto& item:document.GetNamedArray(L"recordings",json::JsonArray{})){
            if(item.ValueType()!=json::JsonValueType::Object)continue;auto object=item.GetObject();CatalogCandidate value;
            value.id=std::wstring(object.GetNamedString(L"id",L""));bool uuid=value.id.size()==36;
            for(size_t i=0;uuid&&i<value.id.size();++i)uuid=(i==8||i==13||i==18||i==23)?value.id[i]==L'-':std::iswxdigit(value.id[i])!=0;
            if(!uuid)continue;
            value.title=std::wstring(object.GetNamedString(L"title",L""));value.version=std::wstring(object.GetNamedString(L"disambiguation",L""));value.duration=object.GetNamedNumber(L"length",0)/1000.;
            for(const auto& credit:object.GetNamedArray(L"artist-credit",json::JsonArray{})){
                if(credit.ValueType()!=json::JsonValueType::Object)continue;auto artist=credit.GetObject();
                value.artist+=std::wstring(artist.GetNamedString(L"name",artist.GetNamedObject(L"artist",json::JsonObject{}).GetNamedString(L"name",L"")));
                value.artist+=std::wstring(artist.GetNamedString(L"joinphrase",L""));
            }
            auto releases=object.GetNamedArray(L"releases",json::JsonArray{});if(releases.Size())value.album=std::wstring(releases.GetObjectAt(0).GetNamedString(L"title",L""));
            value.isrc=isrc;candidates.push_back(std::move(value));
        }
        auto identity=job.track;
        // An ISRC from an already verified Deezer recording identifies the
        // recording even when MusicBrainz omits the edition's remaster label.
        if(!isrc.empty())identity.title=searchTitle(identity.title);
        auto selected=selectRecording(identity,candidates);BpmResult result;
        if(selected.candidates.empty()){result.reason=L"MusicBrainz: "+selected.reason;return result;}
        std::wstring ids;for(const auto& entry:selected.candidates){if(!ids.empty())ids+=L';';ids+=entry.id;}
        auto features=get(client,L"/api/v1/low-level?recording_ids="+encodeQuery(ids)+L"&features=rhythm.bpm",job,L"https://acousticbrainz.org");
        return selectAcousticFeatures(job.track,selected,features);
    }

    BpmResult lookup(http::HttpClient& client,const Job& job){
        BpmResult result;auto key=trackCacheKey(job.track);auto cached=cache.find(key);
        if(cached!=cache.end()&&cached->second.expires>nowMs()){result=cached->second.result;result.fromCache=true;return result;}
        if(job.track.title.empty()||job.track.artist.empty()){result.reason=L"El reproductor no publico titulo y artista suficientes";return result;}
        std::vector<CatalogCandidate> verified;bool failed=false;
        Job primaryJob=job;primaryJob.deadline=nowMs()+3000;
        try{result=lookupDeezer(client,primaryJob,verified);}catch(const OperationCanceled&){throw;}
        catch(const winrt::hresult_error& e){failed=true;result.reason=std::wstring(e.message());}
        catch(const std::exception& e){failed=true;result.reason=wide(e.what());}
        if(!result.found){auto primary=result.reason;
            Job fallback=job;fallback.deadline=nowMs()+4500;
            try{result=lookupBrainz(client,fallback,verified);}catch(const OperationCanceled&){throw;}
            catch(const winrt::hresult_error& e){failed=true;result.reason=std::wstring(e.message());}
            catch(const std::exception& e){failed=true;result.reason=wide(e.what());}
            if(!result.found)result.reason=L"Deezer: "+primary+L"; alternativa MusicBrainz/AcousticBrainz: "+result.reason;
        }
        if(WaitForSingleObject(job.canceled->value,0)==WAIT_OBJECT_0)throw OperationCanceled();
        if(cache.size()>=128)cache.erase(cache.begin());cache[key]={result,nowMs()+(result.found?86400000.:failed?30000.:600000.)};return result;
    }
    void run(){
        bool initialized=false;
        try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);initialized=true;
            filters::HttpBaseProtocolFilter filter;filter.AllowAutoRedirect(false);filter.AllowUI(false);filter.CookieUsageBehavior(filters::HttpCookieUsageBehavior::NoCookies);
            filter.CacheControl().ReadBehavior(filters::HttpCacheReadBehavior::NoCache);filter.CacheControl().WriteBehavior(filters::HttpCacheWriteBehavior::NoCache);
            http::HttpClient client(filter);client.DefaultRequestHeaders().TryAppendWithoutValidation(L"User-Agent",L"DancingKitty/1.1.0 (+https://github.com/Noga-Kurumi/Dancing-Kitty)");client.DefaultRequestHeaders().Accept().ParseAdd(L"application/json");
            HANDLE waits[]={stopEvent.value,workEvent.value};
            while(WaitForMultipleObjects(2,waits,FALSE,INFINITE)==WAIT_OBJECT_0+1){
                std::optional<Job> job;{std::lock_guard<std::mutex> lock(mutex);job=std::move(pending);pending.reset();if(job)active=job->canceled;}
                if(!job)continue;BpmResult result;bool normal=false;
                try{result=lookup(client,*job);normal=true;}
                catch(const OperationCanceled&){continue;}
                catch(const winrt::hresult_error& e){result.reason=std::wstring(e.message());}
                catch(const std::exception& e){result.reason=wide(e.what());}
                if(!normal&&WaitForSingleObject(job->canceled->value,0)!=WAIT_OBJECT_0){if(cache.size()>=128)cache.erase(cache.begin());cache[trackCacheKey(job->track)]={result,nowMs()+30000};}
                {std::lock_guard<std::mutex> lock(mutex);
                    if(WaitForSingleObject(job->canceled->value,0)==WAIT_OBJECT_0)continue;
                    completed=BpmLookupCompletion{job->token,std::move(result)};active.reset();}
                SetEvent(notify);
            }
        }catch(const winrt::hresult_error& e){std::lock_guard<std::mutex> lock(mutex);fatalError=std::wstring(e.message());if(pending){BpmResult r;r.reason=fatalError;completed=BpmLookupCompletion{pending->token,std::move(r)};pending.reset();SetEvent(notify);}}
        catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);fatalError=wide(e.what());if(pending){BpmResult r;r.reason=fatalError;completed=BpmLookupCompletion{pending->token,std::move(r)};pending.reset();SetEvent(notify);}}
        if(initialized)winrt::uninit_apartment();
    }
public:
    explicit BpmCatalog(HANDLE event,std::atomic<uint64_t>* counter=nullptr):notify(event),requestCounter(counter),worker([this]{run();}){}
    ~BpmCatalog(){SetEvent(stopEvent.value);cancel();if(worker.joinable())worker.join();}
    void cancel(){std::lock_guard<std::mutex> lock(mutex);if(active)SetEvent(active->value);if(pending)SetEvent(pending->canceled->value);pending.reset();completed.reset();}
    void request(uint64_t token,const TrackMetadata& track){
        std::lock_guard<std::mutex> lock(mutex);if(active)SetEvent(active->value);if(pending)SetEvent(pending->canceled->value);
        if(!fatalError.empty()){BpmResult r;r.reason=fatalError;completed=BpmLookupCompletion{token,std::move(r)};SetEvent(notify);return;}
        pending=Job{token,track};completed.reset();SetEvent(workEvent.value);
    }
    std::optional<BpmLookupCompletion> take(){std::lock_guard<std::mutex> lock(mutex);auto result=std::move(completed);completed.reset();return result;}
};
}
