// listplug.cpp - Total Commander lister plugin exports for the Markdown viewer
//
// Based on the TC lister plugin sample (listplugsample). The real work is done in
// lister.cpp (controller) and the rendering back-ends (richedit_view.cpp).

#include <windows.h>
#include <string>
#include "listplug.h"
#include "lister.h"

static std::wstring AnsiToWide(const char* s) {
    if (!s) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
    w.resize(n - 1);
    return w;
}

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Lister_Init(hModule);
    }
    return TRUE;
}

extern "C" {

HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    if (!FileToLoad) return NULL;
    if (!(ShowFlags & lcp_forceshow) && !Lister_IsSupportedFile(FileToLoad)) return NULL;
    return Lister_Open(ParentWin, FileToLoad, ShowFlags);
}

HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    std::wstring w = AnsiToWide(FileToLoad);
    return ListLoadW(ParentWin, &w[0], ShowFlags);
}

int __stdcall ListLoadNextW(HWND ParentWin, HWND ListWin, WCHAR* FileToLoad, int ShowFlags) {
    (void)ParentWin;
    if (!FileToLoad) return LISTPLUGIN_ERROR;
    if (!(ShowFlags & lcp_forceshow) && !Lister_IsSupportedFile(FileToLoad)) return LISTPLUGIN_ERROR;
    return Lister_LoadNext(ListWin, FileToLoad, ShowFlags) ? LISTPLUGIN_OK : LISTPLUGIN_ERROR;
}

int __stdcall ListLoadNext(HWND ParentWin, HWND ListWin, char* FileToLoad, int ShowFlags) {
    std::wstring w = AnsiToWide(FileToLoad);
    return ListLoadNextW(ParentWin, ListWin, &w[0], ShowFlags);
}

void __stdcall ListCloseWindow(HWND ListWin) {
    DestroyWindow(ListWin);
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    if (!DetectString || maxlen <= 0) return;
    std::string s = Lister_DetectString();
    if ((int)s.size() >= maxlen) {
        // too long: cut at the last complete alternative
        s = s.substr(0, maxlen - 1);
        size_t p = s.rfind(" | ");
        if (p != std::string::npos) s = s.substr(0, p);
    }
    lstrcpynA(DetectString, s.c_str(), maxlen);
}

int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    return Lister_SearchText(ListWin, SearchString, SearchParameter);
}

int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    std::wstring w = AnsiToWide(SearchString);
    return Lister_SearchText(ListWin, w.c_str(), SearchParameter);
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    return Lister_SendCommand(ListWin, Command, Parameter);
}

int __stdcall ListPrintW(HWND ListWin, WCHAR* FileToPrint, WCHAR* DefPrinter, int PrintFlags, RECT* Margins) {
    (void)DefPrinter; (void)PrintFlags;
    return Lister_Print(ListWin, FileToPrint, Margins);
}

int __stdcall ListPrint(HWND ListWin, char* FileToPrint, char* DefPrinter, int PrintFlags, RECT* Margins) {
    (void)DefPrinter; (void)PrintFlags;
    std::wstring w = AnsiToWide(FileToPrint);
    return Lister_Print(ListWin, w.c_str(), Margins);
}

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    if (dps) Lister_SetIniFile(AnsiToWide(dps->DefaultIniName));
}

} // extern "C"
