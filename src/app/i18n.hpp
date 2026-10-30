#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
namespace catpulse {
struct UiStrings {const wchar_t* language;const wchar_t* startup;const wchar_t* close;const wchar_t* error;bool rtl=false;};
inline constexpr UiStrings translations[]={
    {L"en",L"Start with Windows",L"Close",L"The operation couldn't be completed. Please try again."},
    {L"es",L"Iniciar con Windows",L"Cerrar",L"No se pudo completar la operación. Intentá de nuevo."},
    {L"pt",L"Iniciar com o Windows",L"Fechar",L"Não foi possível concluir a operação. Tente novamente."},
    {L"fr",L"Démarrer avec Windows",L"Fermer",L"L’opération n’a pas pu être effectuée. Réessayez."},
    {L"de",L"Mit Windows starten",L"Schließen",L"Der Vorgang konnte nicht abgeschlossen werden. Bitte erneut versuchen."},
    {L"it",L"Avvia con Windows",L"Chiudi",L"Impossibile completare l’operazione. Riprova."},
    {L"nl",L"Starten met Windows",L"Sluiten",L"De bewerking kon niet worden voltooid. Probeer het opnieuw."},
    {L"pl",L"Uruchamiaj z systemem Windows",L"Zamknij",L"Nie można ukończyć operacji. Spróbuj ponownie."},
    {L"tr",L"Windows ile başlat",L"Kapat",L"İşlem tamamlanamadı. Lütfen yeniden deneyin."},
    {L"ru",L"Запускать вместе с Windows",L"Закрыть",L"Не удалось выполнить операцию. Попробуйте ещё раз."},
    {L"zh-hans",L"随 Windows 启动",L"关闭",L"无法完成操作，请重试。"},
    {L"zh-hant",L"隨 Windows 啟動",L"關閉",L"無法完成操作，請再試一次。"},
    {L"ja",L"Windows 起動時に実行",L"閉じる",L"操作を完了できませんでした。もう一度お試しください。"},
    {L"ko",L"Windows 시작 시 실행",L"닫기",L"작업을 완료하지 못했습니다. 다시 시도해 주세요."},
    {L"ar",L"التشغيل مع Windows",L"إغلاق",L"تعذّر إكمال العملية. يُرجى المحاولة مجددًا.",true},
    {L"hi",L"Windows के साथ शुरू करें",L"बंद करें",L"कार्रवाई पूरी नहीं हो सकी। कृपया फिर से कोशिश करें।"}
};
inline const UiStrings* matchingLanguage(std::wstring language){
    std::transform(language.begin(),language.end(),language.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
    if(language.rfind(L"zh",0)==0){
        bool traditional=language.find(L"hant")!=std::wstring::npos||language.find(L"-tw")!=std::wstring::npos||language.find(L"-hk")!=std::wstring::npos||language.find(L"-mo")!=std::wstring::npos;
        return &translations[traditional?11:10];
    }
    auto separator=language.find(L'-');if(separator!=std::wstring::npos)language.resize(separator);
    for(const auto& item:translations)if(language==item.language)return &item;
    return nullptr;
}
inline const UiStrings& uiStrings(){
    ULONG count=0,size=0;GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&count,nullptr,&size);
    if(size){std::vector<wchar_t> buffer(size);
        if(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&count,buffer.data(),&size))
            for(const wchar_t* name=buffer.data();*name;name+=wcslen(name)+1)if(auto text=matchingLanguage(name))return *text;
    }
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if(LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(),SORT_DEFAULT),name,LOCALE_NAME_MAX_LENGTH,0))if(auto text=matchingLanguage(name))return *text;
    return translations[0];
}
}
