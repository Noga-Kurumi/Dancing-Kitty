#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <algorithm>
#include "bundle_manifest.hpp"
#include "i18n.hpp"
#include "branding.hpp"
namespace fs=std::filesystem;
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
std::wstring quoted(const std::wstring& value){
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}
        if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}
        else{out.append(slashes,L'\\');out+=c;}slashes=0;
    }
    out.append(slashes*2,L'\\');out+=L'\"';return out;
}
bool sameSize(const fs::path& file,DWORD size){
    WIN32_FILE_ATTRIBUTE_DATA data{};
    return GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&data)&&!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&data.nFileSizeHigh==0&&data.nFileSizeLow==size;
}
void extract(HINSTANCE instance,const BundleFile& item,const fs::path& folder){
    fs::path relative(item.relative);
    if(relative.is_absolute()||std::wstring(item.relative).find(L"..")!=std::wstring::npos)throw std::runtime_error("Invalid package path");
    HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(item.id),RT_RCDATA);
    if(!resource||SizeofResource(instance,resource)!=item.size)throw std::runtime_error("Incomplete package");
    fs::path file=folder/relative;if(sameSize(file,item.size))return;
    HGLOBAL loaded=LoadResource(instance,resource);const BYTE* bytes=static_cast<const BYTE*>(LockResource(loaded));
    if(!bytes)throw std::runtime_error("Cannot read package");
    fs::create_directories(file.parent_path());
    fs::path temporary=file;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    {Handle output;output.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(output.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot prepare application");
        DWORD offset=0;
        while(offset<item.size){DWORD written=0,count=std::min<DWORD>(1024*1024,item.size-offset);
            if(!WriteFile(output.value,bytes+offset,count,&written,nullptr)||written!=count)throw std::runtime_error("Cannot write application");offset+=written;}
    }
    if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot finish application");
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    try{
        wchar_t self[32768]{};if(GetModuleFileNameW(instance,self,32768))SHChangeNotify(SHCNE_UPDATEITEM,SHCNF_PATHW,self,nullptr);
        PWSTR known=nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&known)))throw std::runtime_error("No application data folder");
        fs::path legacy=fs::path(known)/L"CatPulse";
        fs::path root=fs::path(known)/catpulse::AppName;CoTaskMemFree(known);
        fs::path folder=root/L"packages"/kBundleId;fs::create_directories(folder);
        Handle setup;std::wstring mutexName=L"Local\\DancingKitty.Bundle."+std::wstring(kBundleId);
        setup.value=CreateMutexW(nullptr,FALSE,mutexName.c_str());
        if(!setup.value)throw std::runtime_error("Cannot prepare package");
        DWORD wait=WaitForSingleObject(setup.value,30000);
        if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED)throw std::runtime_error("Package is busy");
        for(const auto& item:kBundleFiles)extract(instance,item,folder);
        fs::path position=root/L"position.ini";
        // Preserve the user's size and placement when upgrading from CatPulse.
        CopyFileW((legacy/L"position.ini").c_str(),position.c_str(),TRUE);
        CopyFileW((folder/L"initial-position.ini").c_str(),position.c_str(),TRUE);
        ReleaseMutex(setup.value);
        fs::path application=folder/catpulse::ExecutableName;
        std::wstring command=quoted(application.wstring())+L" --position-file "+quoted(position.wstring());
        int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
        for(int i=1;i<count;++i)command+=L" "+quoted(arguments[i]);LocalFree(arguments);
        STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_SHOWNOACTIVATE;
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(application.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,folder.c_str(),&start,&process))throw std::runtime_error("Cannot start Dancing Kitty");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
    }catch(...){MessageBoxW(nullptr,catpulse::uiStrings().error,catpulse::AppName,MB_OK|MB_ICONERROR);return 1;}
}
