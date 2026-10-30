#include "track_match.hpp"
#include <iostream>
#include <stdexcept>

using namespace kp;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    try{
        TrackMetadata song{L"Prófugos (Remasterizado 2007)",L"Soda Stereo",L"Signos",318};
        CatalogCandidate studio{L"1",L"Prófugos Remasterizado 2007",L"Prófugos",L"Remasterizado 2007",L"Soda Stereo",L"Signos",L"STUDIO",317};
        auto live=studio;live.id=L"2";live.isrc=L"LIVE";live.duration=364;live.album=L"Ruido Blanco";
        check(matchTrack(song,studio)>0,"Studio recording was lost because of accents or edition punctuation");
        check(matchTrack(song,live)==0,"Live recording with same remaster title passed the duration check");
        auto cover=studio;cover.artist=L"Otro artista";check(matchTrack(song,cover)==0,"A cover was accepted as the original artist");
        auto otherEdition=studio;otherEdition.title=L"Prófugos Remasterizado 2017";otherEdition.version=L"Remasterizado 2017";
        check(matchTrack(song,otherEdition)==0,"A different explicit remaster year was accepted");
        auto remix=studio;remix.version=L"Remix Remasterizado 2007";check(matchTrack(song,remix)==0,"A remix was accepted as the studio version");
        auto selected=selectRecording(song,{live,cover,studio});check(selected.candidates.size()==1&&selected.candidates.front().id==L"1","Ranking picked a wrong recording");
        auto duplicate=studio;duplicate.id=L"3";
        selected=selectRecording(song,{studio,duplicate});check(selected.candidates.size()==2,"The same ISRC on different releases was treated as a different recording");
        auto alternative=studio;alternative.id=L"4";alternative.isrc=L"ALTERNATIVE";
        selected=selectRecording(song,{studio,alternative});check(selected.candidates.empty()&&!selected.reason.empty(),"An ambiguous recording was selected arbitrarily");
        TrackMetadata fito{L"11 y 6",L"Fito Paez",L"Giros",179};
        CatalogCandidate fitoStudio{L"5",L"11 Y 6",L"11 Y 6",L"",L"Fito Páez",L"Giros",L"FITO-STUDIO",178};
        check(matchTrack(fito,fitoStudio)>0,"Case or artist accent prevented an exact song match");
        auto fitoLive=fitoStudio;fitoLive.title=L"11 y 6 (En Vivo)";fitoLive.version=L"En Vivo";fitoLive.duration=179;
        check(matchTrack(fito,fitoLive)==0,"A live rendition passed even with a similar duration");
        check(searchTitle(song.title)==L"profugos","Search retains edition labels that hide the intended recording");
        check(trackCacheKey(fito)!=trackCacheKey({L"11 y 6 (En Vivo)",L"Fito Paez",L"Giros",179}),"Cache mixes studio and live versions");
        check(samePlaybackTrack(song,song),"Repeated metadata reloads the active song");
        auto refreshed=song;refreshed.album=L"Updated album";refreshed.durationSeconds=317;
        check(samePlaybackTrack(song,refreshed),"Non-identity metadata resets song analysis");
        auto different=song;different.title=L"No Existes (Remasterizado 2007)";
        check(!samePlaybackTrack(song,different),"A different title retains the previous song's analysis");
        different=song;different.artist=L"Otro artista";
        check(!samePlaybackTrack(song,different),"Same title from another artist retains the previous BPM");
        different=song;different.title=L"Prófugos (En Vivo)";
        check(!samePlaybackTrack(song,different),"A different rendition retains the previous analysis");
        check(!samePlaybackTrack(song,{}),"Missing title incorrectly preserves the previous song");
        std::cout<<"PASS recording matching: edition, duration, artist, remix, duplicate ISRC, ambiguity and accents\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
