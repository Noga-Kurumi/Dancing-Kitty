#include "beat_engine.hpp"
#include "animation_clock.hpp"
#include "resize_geometry.hpp"
#include "i18n.hpp"
#include "startup.hpp"
#include "tray_icon.hpp"
#include "loading_indicator.hpp"
#include "topmost_guard.hpp"
#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <psapi.h>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <utility>
#include <filesystem>
using Microsoft::WRL::ComPtr;
namespace {
double clockMs(){static double scale=[](){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return 1000./f.QuadPart;}();LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart*scale;}
void check(HRESULT result){if(FAILED(result))throw std::runtime_error("No se pudieron cargar los frames PNG de Cat.");}
std::wstring directoryOfExecutable(){std::wstring s(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,s.data(),DWORD(s.size()));s.resize(n);s.resize(s.find_last_of(L"\\/"));return s;}
std::wstring defaultPositionFile(){std::filesystem::path base=directoryOfExecutable();
    if(base.parent_path().filename()==L"packages")base=base.parent_path().parent_path();return (base/L"position.ini").wstring();}
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Frame {
    HBITMAP bitmap=nullptr;BYTE* pixels=nullptr;
    Frame()=default;Frame(const Frame&)=delete;Frame& operator=(const Frame&)=delete;
    Frame(Frame&& other)noexcept:bitmap(std::exchange(other.bitmap,nullptr)),pixels(other.pixels){}
    ~Frame(){if(bitmap)DeleteObject(bitmap);}
};
class Sprites {
    ComPtr<IWICImagingFactory> factory;
    HDC dc=CreateCompatibleDC(nullptr);HGDIOBJ stock=nullptr;
    std::vector<Frame> frames;
    std::vector<uint8_t> loadingBackup;
    std::array<ComPtr<IWICBitmap>,10> originals;
    catpulse::NormalizedBounds visible;
    void prepare(int index){
        auto& frame=frames.at(size_t(index));if(frame.bitmap)return;
        ComPtr<IWICBitmapScaler> scaler;check(factory->CreateBitmapScaler(&scaler));
        check(scaler->Initialize(originals.at(size_t(index)).Get(),width,height,WICBitmapInterpolationModeFant));
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;frame.bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);frame.pixels=static_cast<BYTE*>(pixels);
        if(!frame.bitmap)throw std::runtime_error("No se pudo preparar un frame de la animación.");
        check(scaler->CopyPixels(nullptr,UINT(width*4),UINT(width*height*4),frame.pixels));
        auto b=bounds();int grip=std::clamp(std::min(b.right-b.left,b.bottom-b.top)/8,8,24);
        // Invisible hit pads stay inside the alpha bounds of frame 1. PNG
        // pixels are preserved; only fully transparent corner pixels get A=1.
        for(int corner=0;corner<4;++corner)for(int y=0;y<grip;++y)for(int x=0;x<grip;++x){
            int px=(corner&1)?b.right-1-x:b.left+x,py=(corner&2)?b.bottom-1-y:b.top+y;
            BYTE* pixel=frame.pixels+(size_t(py)*width+px)*4;
            if(pixel[3]==0)pixel[3]=1;
        }
    }
public:
    int width=0,height=0;
    Sprites(){if(!dc)throw std::runtime_error("No se pudo crear la ventana transparente.");stock=GetCurrentObject(dc,OBJ_BITMAP);check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));}
    ~Sprites(){SelectObject(dc,stock);frames.clear();DeleteDC(dc);}
    void load(const std::wstring& directory,int side){
        for(int index=1;index<=10;++index){
            wchar_t name[32];swprintf_s(name,L"\\Cat_%02d.png",index);
            ComPtr<IWICBitmapDecoder> decoder;check(factory->CreateDecoderFromFilename((directory+name).c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
            ComPtr<IWICBitmapFrameDecode> decoded;check(decoder->GetFrame(0,&decoded));
            UINT originalW=0,originalH=0;check(decoded->GetSize(&originalW,&originalH));
            if(!originalW||originalW!=originalH)throw std::runtime_error("Los diez frames deben tener un lienzo cuadrado del mismo tamaño.");
            ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter));
            check(converter->Initialize(decoded.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
            check(factory->CreateBitmapFromSource(converter.Get(),WICBitmapCacheOnLoad,&originals[size_t(index-1)]));
            if(index==1){std::vector<BYTE> pixels(size_t(originalW)*originalH*4);check(originals[0]->CopyPixels(nullptr,originalW*4,UINT(pixels.size()),pixels.data()));
                UINT left=originalW,top=originalH,right=0,bottom=0;
                for(UINT y=0;y<originalH;++y)for(UINT x=0;x<originalW;++x)if(pixels[(size_t(y)*originalW+x)*4+3]){
                    left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
                if(right>left&&bottom>top)visible={double(left)/originalW,double(top)/originalH,double(right)/originalW,double(bottom)/originalH};
            }
        }
        resize(side);
    }
    void resize(int side){
        SelectObject(dc,stock);frames.clear();width=height=std::clamp(side,96,1000);frames.resize(10);
    }
    void draw(HWND window,int frame,int loading=-1){
        prepare(frame);SelectObject(dc,frames.at(size_t(frame)).bitmap);SIZE size{width,height};POINT source{};
        BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        auto submit=[&](){if(!UpdateLayeredWindow(window,nullptr,nullptr,&size,dc,&source,0,&blend,ULW_ALPHA))throw std::runtime_error("No se pudo mostrar el gato transparente.");};
        if(loading>=0){catpulse::LoadingOverlay overlay(frames[size_t(frame)].pixels,width,loading,loadingBackup);submit();}else submit();
    }
    bool hit(int frame,int x,int y)const{
        return corner(x,y)!=catpulse::Corner::None||
            (x>=0&&y>=0&&x<width&&y<height&&frames.at(size_t(frame)).pixels&&frames.at(size_t(frame)).pixels[(size_t(y)*width+x)*4+3]>=16);
    }
    catpulse::Bounds bounds()const{return catpulse::scaledBounds(visible,width);}
    catpulse::Corner corner(int x,int y)const{return catpulse::cornerAtBounds(bounds(),x,y);}
    catpulse::NormalizedBounds normalizedBounds()const{return visible;}
};
struct Resources {
    double previousTime=0,cpu=0,ram=0;uint64_t previousCpu=0;
    void sample(double now){
        if(now-previousTime<1000)return;
        FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);ULARGE_INTEGER a{},b{};
        a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;uint64_t total=a.QuadPart+b.QuadPart;
        if(previousTime)cpu=(total-previousCpu)*.0001/(now-previousTime)*100/std::max<DWORD>(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        previousCpu=total;previousTime=now;
        PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));ram=m.WorkingSetSize/1048576.;
    }
};
class Application {
    std::wstring images,positionFile;
    catpulse::StartupEntry startup{directoryOfExecutable()+L"\\"+catpulse::ExecutableName};
    std::unique_ptr<kp::BeatEngine> engine;
    Sprites sprites;catpulse::AnimationClock animation{clockMs()};catpulse::LoadingClock loadingClock;Handle timer;
    HWND window=nullptr;catpulse::TrayIcon tray;catpulse::TopmostGuard front;int shown=-1,loadingFrame=-1;bool dragging=false;POINT cursorStart{},windowStart{};
    catpulse::Corner resizeCorner=catpulse::Corner::None;int startSide=260;UINT dpi=96;
    std::ofstream diagnostic;Resources resources;double lastLog=-1e9;
    void savePosition(){RECT r;GetWindowRect(window,&r);std::wstring x=std::to_wstring(r.left),y=std::to_wstring(r.top);
        WritePrivateProfileStringW(L"Position",L"x",x.c_str(),positionFile.c_str());WritePrivateProfileStringW(L"Position",L"y",y.c_str(),positionFile.c_str());
        std::wstring side=std::to_wstring(sprites.width);WritePrivateProfileStringW(L"Position",L"size",side.c_str(),positionFile.c_str());}
    void redraw(){if(shown>=0)sprites.draw(window,shown,loadingFrame);}
    void menu(POINT cursor,bool fromTray=false){
        if(dragging){dragging=false;ReleaseCapture();savePosition();}
        const auto& text=catpulse::uiStrings();bool enabled=startup.enabled();HMENU popup=CreatePopupMenu();
        if(!popup)return;
        AppendMenuW(popup,MF_STRING|(enabled?MF_CHECKED:MF_UNCHECKED),1,text.startup);AppendMenuW(popup,MF_STRING,2,text.close);
        HWND previous=GetForegroundWindow();SetForegroundWindow(window);
        SetTimer(window,17,10,nullptr); // Keep animation alive in the native menu's modal loop.
        UINT chosen=TrackPopupMenuEx(popup,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY|(text.rtl?TPM_LAYOUTRTL:0),cursor.x,cursor.y,window,nullptr);
        KillTimer(window,17);
        DestroyMenu(popup);PostMessageW(window,WM_NULL,0,0);
        if(GetForegroundWindow()==window&&previous&&previous!=window&&IsWindow(previous))SetForegroundWindow(previous);
        if(fromTray)tray.returnFocus();
        if(chosen==1&&!startup.set(!enabled))MessageBoxW(window,text.error,catpulse::AppName,MB_OK|MB_ICONERROR);
        if(chosen==2){savePosition();DestroyWindow(window);}
    }
    void log(const kp::BeatStatus& state,double now,bool changed){
        if(!diagnostic||(!changed&&now-lastLog<1000))return;lastLog=now;resources.sample(now);
        RECT rect;GetWindowRect(window,&rect);
        diagnostic<<std::setprecision(12)<<std::boolalpha<<"{\"qpc_ms\":"<<now<<",\"frame\":"<<shown+1
            <<",\"playing\":"<<state.playerPlaying<<",\"bMusic\":"<<state.bMusic<<",\"pulses\":"<<state.beatCount
            <<",\"last_pulse_ms\":"<<state.lastBeatQpcMs<<",\"next_pulse_ms\":"<<state.nextPulseQpcMs<<",\"period_ms\":"<<state.pulseIntervalMs
            <<",\"phase_locked\":"<<state.beatPhaseLocked<<",\"finishing\":"<<animation.finishingLoop()<<",\"parked\":"<<animation.stopped()
            <<",\"loading\":"<<(loadingFrame>=0)<<",\"loading_frame\":"<<loadingFrame
            <<",\"bpm\":"<<state.effectiveBpm<<",\"bpm_estimated\":"<<state.catalog.estimated
            <<",\"lookup_state\":"<<int(state.lookupState)<<",\"front_guard\":"<<front.listening()
            <<",\"tray_registered\":"<<tray.registered()<<",\"tool_window\":"<<bool(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)
            <<",\"app_window\":"<<bool(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_APPWINDOW)
            <<",\"cpu_total_percent\":"<<resources.cpu<<",\"ram_mb\":"<<resources.ram
            <<",\"requests\":"<<state.catalogRequests<<",\"x\":"<<rect.left<<",\"y\":"<<rect.top<<",\"width\":"<<sprites.width<<",\"height\":"<<sprites.height<<"}\n";diagnostic.flush();
    }
    void refresh(){
        auto state=engine->status();double now=clockMs();
        catpulse::Rhythm rhythm{state.playerPlaying&&state.bMusic,state.lastBeatQpcMs,state.pulseIntervalMs,state.pulseScheduleValid?state.nextPulseQpcMs:0};
        int frame=animation.update(now,rhythm);
        int spinner=loadingClock.update(now,state.playerPlaying&&state.bMusic&&state.effectiveBpm<=0&&state.lookupState!=kp::MusicLookupState::Error);
        bool changed=frame!=shown||spinner!=loadingFrame;
        if(changed){shown=frame;loadingFrame=spinner;redraw();}log(state,clockMs(),changed);
        double next=std::min(animation.nextFrameTime(),loadingClock.nextFrameTime(now));
        if(std::isfinite(next)){LARGE_INTEGER due;due.QuadPart=-std::max<LONGLONG>(1,LONGLONG(std::ceil((next-clockMs())*10000)));
            if(!SetWaitableTimer(timer.value,&due,0,nullptr,nullptr,FALSE))throw std::runtime_error("No se pudo programar la animación.");}
        else CancelWaitableTimer(timer.value);
    }
    LRESULT message(UINT id,WPARAM w,LPARAM l){
        if(tray.taskbarCreated&&id==tray.taskbarCreated){tray.install(window,reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE)));return 0;}
        switch(id){
        case catpulse::TopmostGuard::Message:front.raise();return 0;
        case WM_WINDOWPOSCHANGING:{auto position=reinterpret_cast<WINDOWPOS*>(l);
            if(!(position->flags&SWP_NOZORDER))position->hwndInsertAfter=HWND_TOPMOST;break;}
        case catpulse::TrayIcon::Message:{
            UINT event=tray.usesModernEvents()?LOWORD(l):UINT(l);
            if(event==WM_CONTEXTMENU||event==NIN_SELECT||event==NIN_KEYSELECT||(!tray.usesModernEvents()&&(event==WM_RBUTTONUP||event==WM_LBUTTONUP))){
                POINT point=tray.usesModernEvents()?POINT{GET_X_LPARAM(w),GET_Y_LPARAM(w)}:tray.anchor();
                if(point.x==-1&&point.y==-1)point=tray.anchor();menu(point,true);
            }return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:ValidateRect(window,nullptr);return 0;
        case WM_TIMER:if(w==17){refresh();return 0;}break;
        case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
        case WM_NCHITTEST:{POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(window,&p);return shown>=0&&sprites.hit(shown,p.x,p.y)?HTCLIENT:HTTRANSPARENT;}
        case WM_SETCURSOR:{POINT p;GetCursorPos(&p);ScreenToClient(window,&p);auto c=dragging?resizeCorner:sprites.corner(p.x,p.y);
            SetCursor(LoadCursorW(nullptr,c==catpulse::Corner::None?IDC_SIZEALL:(c==catpulse::Corner::TopLeft||c==catpulse::Corner::BottomRight)?IDC_SIZENWSE:IDC_SIZENESW));return TRUE;}
        case WM_LBUTTONDOWN:GetCursorPos(&cursorStart);{RECT r;GetWindowRect(window,&r);windowStart={r.left,r.top};}
            resizeCorner=sprites.corner(GET_X_LPARAM(l),GET_Y_LPARAM(l));startSide=sprites.width;dragging=true;SetCapture(window);return 0;
        case WM_MOUSEMOVE:if(dragging){POINT p;GetCursorPos(&p);int dx=p.x-cursorStart.x,dy=p.y-cursorStart.y;
            if(resizeCorner==catpulse::Corner::None)SetWindowPos(window,HWND_TOPMOST,windowStart.x+dx,windowStart.y+dy,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
            else{auto r=catpulse::resizedVisible({windowStart.x,windowStart.y,startSide},sprites.normalizedBounds(),resizeCorner,dx,dy);
                if(r.size!=sprites.width){sprites.resize(r.size);SetWindowPos(window,HWND_TOPMOST,r.x,r.y,r.size,r.size,SWP_NOACTIVATE);redraw();}}}return 0;
        case WM_LBUTTONUP:if(dragging){dragging=false;ReleaseCapture();savePosition();}return 0;
        case WM_CAPTURECHANGED:dragging=false;return 0;
        case WM_RBUTTONUP:case WM_CONTEXTMENU:{POINT cursor;GetCursorPos(&cursor);menu(cursor);return 0;}
        case WM_CLOSE:savePosition();DestroyWindow(window);return 0;
        case WM_DPICHANGED:{int side=MulDiv(sprites.width,HIWORD(w),dpi);dpi=HIWORD(w);sprites.resize(side);side=sprites.width;auto r=reinterpret_cast<RECT*>(l);
            SetWindowPos(window,HWND_TOPMOST,r->left,r->top,side,side,SWP_NOACTIVATE);redraw();return 0;}
        case WM_DISPLAYCHANGE:{RECT r;GetWindowRect(window,&r);MONITORINFO m{};m.cbSize=sizeof(m);GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&m);
            if(r.right<m.rcWork.left+32||r.left>m.rcWork.right-32||r.bottom<m.rcWork.top+32||r.top>m.rcWork.bottom-32)
                SetWindowPos(window,HWND_TOPMOST,m.rcWork.right-sprites.width-24,m.rcWork.bottom-sprites.height-24,0,0,SWP_NOSIZE|SWP_NOACTIVATE);return 0;}
        case WM_DESTROY:front.stop();tray.remove();PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(window,id,w,l);
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT id,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(id==WM_NCCREATE){self=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,id,w,l);
        try{return self->message(id,w,l);}catch(...){DestroyWindow(hwnd);return 0;}
    }
public:
    Application(const std::wstring& models,std::wstring frames,const std::wstring& logPath,const std::wstring& position,bool offline):images(std::move(frames)),positionFile(position.empty()?defaultPositionFile():position),engine(std::make_unique<kp::BeatEngine>(models,offline)){
        if(!logPath.empty()){diagnostic.open(logPath);if(!diagnostic)throw std::runtime_error("No se pudo abrir el diagnóstico.");}
        timer.value=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer.value)timer.value=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        if(!timer.value)throw std::runtime_error("No se pudo crear el reloj de la animación.");
    }
    ~Application(){engine->stop();}
    int run(HINSTANCE instance){
        dpi=GetDpiForSystem();int side=std::clamp<int>(int(GetPrivateProfileIntW(L"Position",L"size",MulDiv(260,dpi,96),positionFile.c_str())),96,1000);sprites.load(images,side);
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=L"DancingKittyOverlay";cls.hCursor=LoadCursorW(nullptr,IDC_SIZEALL);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
        if(!RegisterClassW(&cls))throw std::runtime_error("No se pudo crear la ventana del gato.");
        RECT area;SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
        int x=int(GetPrivateProfileIntW(L"Position",L"x",area.right-side-28,positionFile.c_str()));
        int y=int(GetPrivateProfileIntW(L"Position",L"y",area.bottom-side-28,positionFile.c_str()));
        RECT proposed{x,y,x+side,y+side};MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromRect(&proposed,MONITOR_DEFAULTTONEAREST),&monitor);
        x=std::clamp<int>(x,monitor.rcWork.left-side+64,monitor.rcWork.right-64);y=std::clamp<int>(y,monitor.rcWork.top,monitor.rcWork.bottom-64);
        DWORD extra=WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE;
        window=CreateWindowExW(extra,cls.lpszClassName,catpulse::AppName,WS_POPUP,x,y,side,side,nullptr,nullptr,instance,this);
        if(!window)throw std::runtime_error("No se pudo abrir la ventana del gato.");
        // Start the idle finishing loop after image decoding, so startup work
        // cannot consume the first frames before the window becomes visible.
        tray.install(window,instance);
        animation=catpulse::AnimationClock(clockMs());refresh();
        ShowWindow(window,SW_SHOWNOACTIVATE);SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        front.start(window);
        engine->start();
        startup.updateExisting();
        // This ten-frame visual follows every original beat. The engine's
        // default half-time clap mode remains available to other consumers.
        engine->setPulseMode(kp::PulseMode::EveryBeat);
        HANDLE handles[]={engine->nativeEventHandle(),timer.value};
        for(;;){
            DWORD event=MsgWaitForMultipleObjectsEx(2,handles,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            if(event==WAIT_FAILED)throw std::runtime_error("No se pudo esperar el siguiente frame.");
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)return int(msg.wParam);TranslateMessage(&msg);DispatchMessageW(&msg);}
            if(!IsWindow(window))return 0;refresh();
        }
    }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Handle single;single.value=CreateMutexW(nullptr,TRUE,L"Local\\CatPulse.DesktopOverlay");if(single.value&&GetLastError()==ERROR_ALREADY_EXISTS)return 0;
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(apartment))return 1;
    int result=1;
    try{
        auto base=directoryOfExecutable();std::wstring models=base+L"\\runtime",images=base+L"\\frames",diagnostic,position;bool offline=false;
        int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
        for(int i=1;i<count;++i){std::wstring arg=args[i];if(arg==L"--offline")offline=true;
            else if(i+1<count&&arg==L"--models")models=args[++i];else if(i+1<count&&arg==L"--frames")images=args[++i];else if(i+1<count&&arg==L"--diagnostics")diagnostic=args[++i];else if(i+1<count&&arg==L"--position-file")position=args[++i];}
        LocalFree(args);Application app(models,images,diagnostic,position,offline);result=app.run(instance);
    }catch(const std::exception&){MessageBoxW(nullptr,catpulse::uiStrings().error,catpulse::AppName,MB_OK|MB_ICONERROR);}
    CoUninitialize();return result;
}
