#pragma once
#include "common.hpp"
#include "media_types.hpp"
#include <cwctype>
#include <algorithm>
#include <cmath>
#include <vector>

namespace kp {
// Unicode decomposition makes Páez/Paez comparable without accepting a
// different artist. Punctuation and spacing are not recording identifiers.
inline std::wstring foldedWords(const std::wstring& text){
    if(text.empty())return {};
    int n=NormalizeString(NormalizationD,text.data(),int(text.size()),nullptr,0);
    std::wstring decomposed=n>0?std::wstring(size_t(n),L'\0'):text;
    if(n>0){int actual=NormalizeString(NormalizationD,text.data(),int(text.size()),decomposed.data(),n);if(actual>0)decomposed.resize(size_t(actual));else decomposed=text;}
    std::wstring lower(decomposed.size(),L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,decomposed.data(),int(decomposed.size()),lower.data(),int(lower.size()),nullptr,nullptr,0))lower=decomposed;
    std::wstring result;bool space=false;
    for(wchar_t c:lower){WORD type=0,mark=0;GetStringTypeW(CT_CTYPE1,&c,1,&type);GetStringTypeW(CT_CTYPE3,&c,1,&mark);
        if(mark&C3_NONSPACING)continue;
        if(type&(C1_ALPHA|C1_DIGIT)){if(space&&!result.empty())result+=L' ';result+=c;space=false;}else space=true;
    }
    return result;
}
inline std::wstring compact(const std::wstring& text){auto result=foldedWords(text);result.erase(std::remove(result.begin(),result.end(),L' '),result.end());return result;}
inline bool samePlaybackTrack(const TrackMetadata& before,const TrackMetadata& after){
    if(before.title.empty()||after.title.empty()||compact(before.title)!=compact(after.title))return false;
    return before.artist.empty()||after.artist.empty()||compact(before.artist)==compact(after.artist);
}
inline bool word(const std::wstring& text,const wchar_t* value){return (L" "+foldedWords(text)+L" ").find(L" "+std::wstring(value)+L" ")!=std::wstring::npos;}
inline unsigned rendition(const std::wstring& text){
    unsigned result=0;
    if(word(text,L"live")||word(text,L"vivo")||word(text,L"directo"))result|=1;
    if(word(text,L"acoustic")||word(text,L"acustico")||word(text,L"acustica")||word(text,L"unplugged"))result|=2;
    if(word(text,L"remix")||word(text,L"mix"))result|=4;
    if(word(text,L"edit")||word(text,L"edited"))result|=8;
    if(word(text,L"instrumental"))result|=16;
    if(word(text,L"karaoke"))result|=32;
    return result;
}
inline bool remastered(const std::wstring& text){return word(text,L"remastered")||word(text,L"remasterizado")||word(text,L"remaster");}
inline int versionYear(const std::wstring& text){
    auto words=L" "+foldedWords(text)+L" ";
    for(size_t i=1;i+4<words.size();++i)if(words[i-1]==L' '&&words[i+4]==L' '){
        bool digits=true;int year=0;for(size_t j=0;j<4;++j){wchar_t c=words[i+j];if(c<L'0'||c>L'9'){digits=false;break;}year=year*10+int(c-L'0');}
        if(digits&&year>=1900&&year<=2100)return year;
    }
    return 0;
}
inline std::wstring searchTitle(std::wstring text){
    for(size_t p=0;p<text.size();){
        if(text[p]!=L'('&&text[p]!=L'['){++p;continue;}
        auto end=text.find(text[p]==L'('?L')':L']',p+1);if(end==std::wstring::npos)break;
        auto suffix=text.substr(p,end-p+1);
        if(rendition(suffix)||remastered(suffix))text.erase(p,end-p+1);else p=end+1;
    }
    auto dash=text.rfind(L" - ");if(dash!=std::wstring::npos){auto suffix=text.substr(dash+3);if(rendition(suffix)||remastered(suffix))text.erase(dash);}
    auto words=foldedWords(text);
    for(const auto* tag:{L" remastered",L" remasterizado",L" remaster"}){auto p=words.find(tag);if(p!=std::wstring::npos){words.erase(p);break;}}
    return words;
}
inline std::wstring baseTitle(const std::wstring& text){return compact(searchTitle(text));}
struct CatalogCandidate {
    std::wstring id,title,shortTitle,version,artist,album,isrc;
    double duration=0;
};
inline double matchTrack(const TrackMetadata& query,const CatalogCandidate& candidate){
    if(query.title.empty()||query.artist.empty()||compact(query.artist)!=compact(candidate.artist))return 0;
    auto candidateTitle=candidate.shortTitle.empty()?candidate.title:candidate.shortTitle;
    if(baseTitle(query.title)!=baseTitle(candidateTitle))return 0;
    if(rendition(query.title)!=rendition(candidate.title+L" "+candidate.version))return 0;
    if(remastered(query.title)&&!remastered(candidate.title+L" "+candidate.version))return 0;
    if((remastered(query.title)||rendition(query.title))&&versionYear(query.title)>0&&versionYear(query.title)!=versionYear(candidate.title+L" "+candidate.version))return 0;
    if(query.durationSeconds>0&&(candidate.duration<=0||std::abs(query.durationSeconds-candidate.duration)>std::max(3.,query.durationSeconds*.015)))return 0;
    double score=80;
    if(compact(query.title)==compact(candidate.title))score+=8;
    if(!query.album.empty()&&compact(query.album)==compact(candidate.album))score+=8;
    if(query.durationSeconds>0)score+=4;
    return score;
}
inline std::wstring trackCacheKey(const TrackMetadata& track){return compact(track.artist)+L"|"+compact(track.title)+L"|"+compact(track.album)+L"|"+std::to_wstring(int64_t(std::llround(track.durationSeconds)));}
struct CandidateSelection {std::vector<CatalogCandidate> candidates;std::wstring reason;double confidence=0;};
inline CandidateSelection selectRecording(const TrackMetadata& track,const std::vector<CatalogCandidate>& input){
    struct Ranked {CatalogCandidate candidate;double score;};std::vector<Ranked> ranked;
    for(const auto& candidate:input){double score=matchTrack(track,candidate);if(score>0)ranked.push_back({candidate,score});}
    std::sort(ranked.begin(),ranked.end(),[](const Ranked& a,const Ranked& b){return a.score>b.score;});
    CandidateSelection result;if(ranked.empty()){result.reason=L"No se encontro una grabacion compatible con titulo, artista y duracion";return result;}
    auto identity=[](const CatalogCandidate& c){return c.isrc.empty()?L"id:"+c.id:L"isrc:"+c.isrc;};
    auto chosen=identity(ranked.front().candidate);
    for(const auto& item:ranked){
        if(item.score<ranked.front().score-3)break;
        if(identity(item.candidate)!=chosen){result.reason=L"Hay varias grabaciones posibles; el BPM no se asigna por aproximacion";return result;}
    }
    for(const auto& item:ranked)if(identity(item.candidate)==chosen&&result.candidates.size()<3)result.candidates.push_back(item.candidate);
    result.confidence=std::min(1.,ranked.front().score/100.);return result;
}
}
