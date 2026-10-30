#pragma once
#include <windows.h>
#include <shellapi.h>
#include "branding.hpp"
namespace catpulse {
class TrayIcon {
    NOTIFYICONDATAW data{};
    bool added=false,modern=false;
public:
    static constexpr UINT Message=WM_APP+1;
    const UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    TrayIcon()=default;
    TrayIcon(const TrayIcon&)=delete;
    TrayIcon& operator=(const TrayIcon&)=delete;
    ~TrayIcon(){remove();}
    void install(HWND owner,HINSTANCE instance){
        remove();
        data={};data.cbSize=sizeof(data);data.hWnd=owner;data.uID=1;
        data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;data.uCallbackMessage=Message;
        data.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
        wcscpy_s(data.szTip,AppName);
        added=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
        data.uVersion=NOTIFYICON_VERSION_4;
        modern=added&&Shell_NotifyIconW(NIM_SETVERSION,&data)!=FALSE;
    }
    void remove(){if(added){Shell_NotifyIconW(NIM_DELETE,&data);added=false;}}
    void returnFocus(){if(added)Shell_NotifyIconW(NIM_SETFOCUS,&data);}
    bool registered()const{return added;}
    bool usesModernEvents()const{return modern;}
    POINT anchor()const{
        NOTIFYICONIDENTIFIER id{};id.cbSize=sizeof(id);id.hWnd=data.hWnd;id.uID=data.uID;
        RECT rect{};if(SUCCEEDED(Shell_NotifyIconGetRect(&id,&rect)))return {(rect.left+rect.right)/2,rect.top};
        POINT cursor{};GetCursorPos(&cursor);return cursor;
    }
};
}
