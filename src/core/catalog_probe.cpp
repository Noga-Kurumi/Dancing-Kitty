#include "bpm_catalog.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc<4)return 2;winrt::init_apartment(winrt::apartment_type::multi_threaded);
    kp::EventHandle ready;std::atomic<uint64_t> requests{0};kp::BpmCatalog catalog(ready.value,&requests);
    kp::TrackMetadata track{argv[1],argv[2],argc>4?argv[4]:L"",std::wcstod(argv[3],nullptr)};
    catalog.request(1,track);if(WaitForSingleObject(ready.value,40000)!=WAIT_OBJECT_0){std::cerr<<"LOOKUP_TIMEOUT\n";return 1;}
    auto result=catalog.take();if(!result)return 1;
    std::cout<<"bpm="<<result->result.bpm<<" found="<<result->result.found<<" requests="<<requests.load()<<" source="<<winrt::to_string(result->result.source)<<" reason="<<winrt::to_string(result->result.reason)<<'\n';
    auto before=requests.load();catalog.request(2,track);if(WaitForSingleObject(ready.value,3000)!=WAIT_OBJECT_0)return 1;
    auto cached=catalog.take();if(!cached||!cached->result.fromCache||requests.load()!=before){std::cerr<<"CACHE_FAILED\n";return 1;}
    std::cout<<"CACHE_OK requests="<<requests.load()<<'\n';return 0;
}
