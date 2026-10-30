#pragma once
#include "common.hpp"
#include <winrt/Windows.Foundation.h>
#include <memory>
#include <stdexcept>

namespace kp {
struct EventHandle {
    HANDLE value=nullptr;
    explicit EventHandle(bool manual=false):value(CreateEventW(nullptr,manual,FALSE,nullptr)){
        if(!value)throw std::runtime_error("No se pudo crear el evento del motor");
    }
    ~EventHandle(){if(value)CloseHandle(value);}
    EventHandle(const EventHandle&)=delete;
    EventHandle& operator=(const EventHandle&)=delete;
};
struct OperationCanceled:std::exception {};
struct OperationTimedOut:std::runtime_error {OperationTimedOut():std::runtime_error("La consulta excedio el tiempo limite"){};};

// Each async operation owns its completion event through its callback, so a
// late completion after cancellation cannot signal an already closed handle.
template<class Operation> auto awaitResult(const Operation& operation,HANDLE stop,HANDLE cancel,DWORD timeout=8000){
    auto completed=std::make_shared<EventHandle>(true);
    operation.Completed([completed](const auto&,winrt::Windows::Foundation::AsyncStatus){SetEvent(completed->value);});
    HANDLE waits[]={stop,completed->value,completed->value};DWORD count=2;
    if(cancel){waits[1]=cancel;count=3;}
    DWORD result=WaitForMultipleObjects(count,waits,FALSE,timeout);
    if(result!=WAIT_OBJECT_0+count-1){
        try{operation.Cancel();}catch(...){}
        if(result==WAIT_TIMEOUT)throw OperationTimedOut();
        if(result==WAIT_FAILED)throw std::runtime_error("Fallo al esperar una operacion de Windows");
        throw OperationCanceled();
    }
    return operation.GetResults();
}
}
