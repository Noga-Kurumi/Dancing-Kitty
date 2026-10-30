#pragma once
#include <windows.h>
#include <string>
#include <utility>
#include <algorithm>
#include <cwctype>
#include "branding.hpp"
namespace catpulse {
class StartupEntry {
    static constexpr const wchar_t* key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    std::wstring command;
    std::wstring current(const wchar_t* name=AppName)const{
        DWORD size=0;if(RegGetValueW(HKEY_CURRENT_USER,key,name,RRF_RT_REG_SZ,nullptr,nullptr,&size)!=ERROR_SUCCESS)return {};
        std::wstring value(size/sizeof(wchar_t),L'\0');
        if(RegGetValueW(HKEY_CURRENT_USER,key,name,RRF_RT_REG_SZ,nullptr,value.data(),&size)!=ERROR_SUCCESS)return {};
        while(!value.empty()&&value.back()==L'\0')value.pop_back();return value;
    }
    static bool owned(std::wstring value,const wchar_t* folder){
        std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
        return value.find(folder)!=std::wstring::npos;
    }
    bool legacyEnabled()const{return owned(current(L"CatPulse"),L"\\catpulse\\packages\\");}
    void removeLegacy()const{if(legacyEnabled())RegDeleteKeyValueW(HKEY_CURRENT_USER,key,L"CatPulse");}
public:
    explicit StartupEntry(std::wstring executable):command(L"\""+std::move(executable)+L"\""){}
    bool enabled()const{return !current().empty()||legacyEnabled();}
    bool set(bool on)const{
        if(!on){LSTATUS result=RegDeleteKeyValueW(HKEY_CURRENT_USER,key,AppName);if(result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND){removeLegacy();return true;}return false;}
        if(command.size()>260)return false;
        HKEY opened=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&opened,nullptr)!=ERROR_SUCCESS)return false;
        LSTATUS result=RegSetValueExW(opened,AppName,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),DWORD((command.size()+1)*sizeof(wchar_t)));
        RegCloseKey(opened);if(result==ERROR_SUCCESS){removeLegacy();return true;}return false;
    }
    void updateExisting()const{
        auto old=current();
        if(old.empty()){if(legacyEnabled())set(true);}
        else if(old!=command&&owned(old,L"\\dancing kitty\\packages\\"))set(true);
        else if(old==command)removeLegacy();
    }
};
}
