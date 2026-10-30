#pragma once
#include <windows.h>

namespace catpulse {
class TopmostGuard {
    HWND window=nullptr;
    HWINEVENTHOOK hook=nullptr;
    inline static HWND target=nullptr;
    inline static bool pending=false;
    static void CALLBACK changed(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){
        if(target&&!pending&&IsWindowVisible(target))pending=PostMessageW(target,Message,0,0)!=FALSE;
    }
public:
    static constexpr UINT Message=WM_APP+2;
    ~TopmostGuard(){stop();}
    void start(HWND owner){
        window=target=owner;
        hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,changed,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        raise();
    }
    void stop(){target=nullptr;pending=false;if(hook){UnhookWinEvent(hook);hook=nullptr;}}
    void raise(){pending=false;if(window&&IsWindowVisible(window))SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);}
    bool listening()const{return hook!=nullptr;}
};
}
