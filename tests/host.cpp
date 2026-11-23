// host.cpp - minimal fake "Lister" used to smoke-test the plugin DLL (runs under Wine or Windows)
//   host.exe <plugin.wlx64> <file.md> [screenshot-prefix]
#include <windows.h>
#include <richedit.h>
#include <cstdio>
#include <string>
#include <vector>

typedef HWND (__stdcall *ListLoadW_t)(HWND, WCHAR*, int);
typedef int  (__stdcall *ListLoadNextW_t)(HWND, HWND, WCHAR*, int);
typedef int  (__stdcall *ListSearchTextW_t)(HWND, WCHAR*, int);
typedef int  (__stdcall *ListSendCommand_t)(HWND, int, int);
typedef void (__stdcall *ListCloseWindow_t)(HWND);
typedef void (__stdcall *ListGetDetectString_t)(char*, int);
struct DPS { int size; DWORD lo, hi; char ini[MAX_PATH]; };
typedef void (__stdcall *ListSetDefaultParams_t)(DPS*);

static std::vector<std::string> g_log;
static int g_fail = 0;

static LRESULT CALLBACK ParentProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    char b[128];
    if (m == WM_COMMAND && HIWORD(w) == 0xFFFE) { sprintf(b, "percent %d", LOWORD(w)); g_log.push_back(b); return 0; }
    if (m == WM_KEYDOWN) { sprintf(b, "key %d", (int)w); g_log.push_back(b); return 0; }
    if (m == WM_SIZE) { HWND c = GetWindow(h, GW_CHILD); if (c) MoveWindow(c, 0, 0, LOWORD(l), HIWORD(l), TRUE); }
    return DefWindowProcW(h, m, w, l);
}

static void Pump() { MSG msg; while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }

static bool Logged(const char* s) {
    for (size_t i = 0; i < g_log.size(); i++) if (g_log[i] == s) return true;
    return false;
}
static void Expect(bool ok, const char* what) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) g_fail++; }

static std::wstring EditText(HWND edit) {
    GETTEXTLENGTHEX gl = { GTL_DEFAULT, 1200 };
    int n = (int)SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    std::wstring s(n + 1, 0);
    GETTEXTEX gt = { (DWORD)((n + 1) * 2), GT_DEFAULT, 1200, NULL, NULL };
    SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)&s[0]);
    s.resize(wcslen(s.c_str()));
    return s;
}

static void AppendToFile(const wchar_t* path, const char* text) {
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(f, text, (DWORD)strlen(text), &w, NULL);
    CloseHandle(f);
}

// Sends a key with Ctrl held (sets this thread's keyboard state, which GetKeyState reads)
static void CtrlKey(HWND h, WPARAM vk) {
    BYTE ks[256];
    GetKeyboardState(ks);
    ks[VK_CONTROL] = 0x80; ks[VK_LCONTROL] = 0x80;
    SetKeyboardState(ks);
    SendMessageW(h, WM_KEYDOWN, vk, 0);
    ks[VK_CONTROL] = 0; ks[VK_LCONTROL] = 0;
    SetKeyboardState(ks);
    Pump();
}

static void Shot(HWND h, const char* prefix, const char* name) {
    if (!prefix) return;
    Pump(); Sleep(300); Pump();
    RECT r; GetWindowRect(h, &r);
    char cmd[512];
    sprintf(cmd, "%s_%s", prefix, name);
    FILE* f = fopen(cmd, "w"); if (f) { fprintf(f, "%ld %ld %ld %ld\n", r.left, r.top, r.right, r.bottom); fclose(f); }
    printf("SHOT %s\n", name); fflush(stdout);
    Sleep(2500);  // the driver script grabs the screen when it sees SHOT
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { printf("usage: host plugin file [shotprefix]\n"); return 2; }
    const char* shot = NULL; char shotbuf[MAX_PATH];
    if (argc > 3) { WideCharToMultiByte(CP_ACP, 0, argv[3], -1, shotbuf, MAX_PATH, 0, 0); shot = shotbuf; }
    HMODULE lib = LoadLibraryW(argv[1]);
    Expect(lib != NULL, "LoadLibrary");
    if (!lib) return 1;
    ListLoadW_t ListLoadW = (ListLoadW_t)GetProcAddress(lib, "ListLoadW");
    ListLoadNextW_t ListLoadNextW = (ListLoadNextW_t)GetProcAddress(lib, "ListLoadNextW");
    ListSearchTextW_t ListSearchTextW = (ListSearchTextW_t)GetProcAddress(lib, "ListSearchTextW");
    ListSendCommand_t ListSendCommand = (ListSendCommand_t)GetProcAddress(lib, "ListSendCommand");
    ListCloseWindow_t ListCloseWindow = (ListCloseWindow_t)GetProcAddress(lib, "ListCloseWindow");
    ListGetDetectString_t ListGetDetectString = (ListGetDetectString_t)GetProcAddress(lib, "ListGetDetectString");
    ListSetDefaultParams_t ListSetDefaultParams = (ListSetDefaultParams_t)GetProcAddress(lib, "ListSetDefaultParams");
    Expect(ListLoadW && ListLoadNextW && ListSearchTextW && ListSendCommand && ListCloseWindow && ListGetDetectString && ListSetDefaultParams, "exports");

    DPS d; memset(&d, 0, sizeof(d)); d.size = sizeof(d); d.lo = 2; d.hi = 2;
    GetFullPathNameA("lsplugin_test.ini", MAX_PATH, d.ini, NULL);
    ListSetDefaultParams(&d);
    char det[512]; ListGetDetectString(det, sizeof(det));
    printf("detect: %s\n", det);
    Expect(strstr(det, "EXT=\"MD\"") != NULL, "detect string");

    WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ParentProc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"TLister";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND parent = CreateWindowW(L"TLister", L"Lister - test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 900, 700, NULL, NULL, wc.hInstance, NULL);

    // unsupported extension rejected unless forceshow
    Expect(ListLoadW(parent, (WCHAR*)L"c:\\x\\file.txt", 0) == NULL, "reject .txt");

    // work on a copy: the reload tests modify the file
    wchar_t tmpDir[MAX_PATH], copy[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    swprintf(copy, MAX_PATH, L"%lsmdv_host_test.md", tmpDir);
    Expect(CopyFileW(argv[2], copy, FALSE) != 0, "copy test file");
    HWND lw = ListLoadW(parent, copy, 1 /*wrap*/);
    Expect(lw != NULL, "ListLoadW");
    if (!lw) return 1;
    HWND edit = GetWindow(lw, GW_CHILD);
    Expect(edit != NULL, "RichEdit child");
    SetFocus(lw);
    Pump();
    Expect(GetFocus() == edit, "focus forwarded to view");
    std::wstring t = EditText(edit);
    Expect(t.find(L"Markdown Viewer") != std::wstring::npos && t.find(L"# Markdown") == std::wstring::npos, "preview mode shows formatted text");
    Shot(parent, shot, "preview_top.png");

    // navigation
    g_log.clear();
    int first0 = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(edit, WM_KEYDOWN, VK_NEXT, 0); Pump();
    int first1 = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    Expect(first1 > first0, "PgDn scrolls");
    SendMessageW(edit, WM_KEYDOWN, VK_DOWN, 0); Pump();
    int first2 = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    Expect(first2 == first1 + 1, "Down scrolls one line");
    SendMessageW(edit, WM_KEYDOWN, VK_UP, 0); Pump();
    Expect((int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0) == first1, "Up scrolls one line");
    SendMessageW(edit, WM_KEYDOWN, VK_PRIOR, 0); Pump();
    Expect((int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0) < first1, "PgUp scrolls");
    bool percentSeen = false;
    for (size_t i = 0; i < g_log.size(); i++) if (g_log[i].compare(0, 7, "percent") == 0) percentSeen = true;
    Expect(percentSeen, "percent notifications sent to Lister");
    Shot(parent, shot, "preview_pgdn.png");

    // mode toggle: Ctrl+M, Ctrl+Tab, Ctrl+U (each toggles in both directions)
    const char* names[3] = { "Ctrl+M", "Ctrl+Tab", "Ctrl+U" };
    WPARAM keys[3] = { 'M', VK_TAB, 'U' };
    for (int k = 0; k < 3; k++) {
        char msg[64];
        CtrlKey(edit, keys[k]);
        sprintf(msg, "%s -> source mode", names[k]);
        Expect(EditText(edit).find(L"# Markdown Viewer *test*") != std::wstring::npos, msg);
        if (k == 0) Shot(parent, shot, "source.png");
        CtrlKey(edit, keys[k]);
        t = EditText(edit);
        sprintf(msg, "%s -> preview mode", names[k]);
        Expect(t.find(L"# Markdown Viewer") == std::wstring::npos && t.find(L"Markdown Viewer") != std::wstring::npos, msg);
    }
    // S and P belong to Lister now
    g_log.clear();
    SendMessageW(edit, WM_KEYDOWN, 'S', 0); SendMessageW(edit, WM_KEYDOWN, 'P', 0); Pump();
    Expect(Logged("key 83") && Logged("key 80"), "S and P forwarded to Lister");
    Expect(EditText(edit).find(L"# Markdown Viewer *test*") == std::wstring::npos, "S/P do not switch mode");

    // help page
    g_log.clear();
    SendMessageW(edit, WM_KEYDOWN, 'H', 0); Pump();
    Expect(Logged("key 72") && EditText(edit).find(L"Toggle between preview") == std::wstring::npos, "H forwarded to Lister, no help");
    g_log.clear();
    SendMessageW(edit, WM_KEYDOWN, VK_F1, 0); Pump();
    Expect(EditText(edit).find(L"Toggle between preview") != std::wstring::npos, "F1 shows help (embedded README)");
    Expect(!Logged("key 112"), "F1 not forwarded to Lister");
    Shot(parent, shot, "help.png");
    SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0); Pump();
    t = EditText(edit);
    Expect(t.find(L"Toggle between preview") == std::wstring::npos && t.find(L"deep two") != std::wstring::npos, "Esc leaves help");
    Expect(!Logged("key 27"), "Esc in help not forwarded to Lister");
    SendMessageW(edit, WM_KEYDOWN, VK_F1, 0); Pump();
    SendMessageW(edit, WM_KEYDOWN, VK_F1, 0); Pump();
    Expect(EditText(edit).find(L"deep two") != std::wstring::npos, "F1 again leaves help");

    // keys forwarded to Lister
    g_log.clear();
    SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
    SendMessageW(edit, WM_KEYDOWN, VK_F3, 0);
    SendMessageW(edit, WM_KEYDOWN, VK_F7, 0);
    Pump();
    Expect(Logged("key 27"), "Esc forwarded to Lister");
    Expect(Logged("key 114"), "F3 forwarded to Lister");
    Expect(Logged("key 118"), "F7 forwarded to Lister");
    g_log.clear();
    SendMessageW(edit, WM_SYSKEYDOWN, VK_F7, 1 << 29); Pump();
    Expect(Logged("key 118"), "Alt+F7 forwarded as F7");

    // search (preview)
    Expect(ListSearchTextW(lw, (WCHAR*)L"deep two", 1) == 0, "search finds 'deep two'");
    CHARRANGE cr; SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    std::wstring all = EditText(edit);
    Expect(cr.cpMax - cr.cpMin == 8, "selection covers match");
    Expect(ListSearchTextW(lw, (WCHAR*)L"Deep Two", 1 | 2) == 1, "match case respected");
    Expect(ListSearchTextW(lw, (WCHAR*)L"nested", 1) == 0, "find first 'nested'");
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr); LONG firstHit = cr.cpMin;
    Expect(ListSearchTextW(lw, (WCHAR*)L"nested", 0) == 0, "find next 'nested'");
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    Expect(cr.cpMin > firstHit, "find next moves forward");
    Expect(ListSearchTextW(lw, (WCHAR*)L"nested", 8) == 0, "find previous");
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    Expect(cr.cpMin == firstHit, "find previous goes back");
    Expect(ListSearchTextW(lw, (WCHAR*)L"zzzznotthere", 1) == 1, "not found -> error");

    // commands
    Expect(ListSendCommand(lw, 4 /*lc_setpercent*/, 100) == 0, "lc_setpercent");
    Shot(parent, shot, "preview_end.png");
    Expect(ListSendCommand(lw, 3 /*lc_selectall*/, 0) == 0, "lc_selectall");
    Expect(ListSendCommand(lw, 2 /*lc_newparams*/, 128 /*dark*/) == 0, "lc_newparams dark");
    ListSendCommand(lw, 4, 0);
    Shot(parent, shot, "dark.png");

    // resize triggers re-render without crashing
    MoveWindow(parent, 0, 0, 600, 600, TRUE); Pump(); Sleep(400); Pump();
    Expect(IsWindow(lw), "resize ok");

    // F2: manual reload from disk
    ListSendCommand(lw, 2 /*lc_newparams*/, 1);
    AppendToFile(copy, "\n\nReloadMarkerOne\n");
    g_log.clear();
    SendMessageW(edit, WM_KEYDOWN, VK_F2, 0); Pump();
    Expect(EditText(edit).find(L"ReloadMarkerOne") != std::wstring::npos, "F2 reloads the file");
    Expect(!Logged("key 113"), "F2 handled by the plugin (not passed to Lister)");

    // auto-reload: change the file and wait (polling ~0.7 s, change must be stable one interval)
    Sleep(50);
    AppendToFile(copy, "\nAutoReloadMarkerTwo\n");
    bool autoOk = false;
    for (int i = 0; i < 40 && !autoOk; i++) {
        Sleep(100); Pump();
        autoOk = EditText(edit).find(L"AutoReloadMarkerTwo") != std::wstring::npos;
    }
    Expect(autoOk, "auto-reload picks up the change");

    // load next file
    Expect(ListLoadNextW(parent, lw, argv[2], 0) == 0, "ListLoadNextW");

    ListCloseWindow(lw);
    DeleteFileW(copy);
    Pump();
    Expect(!IsWindow(lw), "ListCloseWindow");
    DestroyWindow(parent);
    printf(g_fail ? "FAILURES: %d\n" : "ALL OK %d\n", g_fail);
    return g_fail ? 1 : 0;
}
