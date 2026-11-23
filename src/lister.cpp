// lister.cpp - controller between Total Commander's Lister and a rendering back-end
//
// Keys (handled while the viewer has the focus):
//   Ctrl+M, Ctrl+Tab, Ctrl+U   toggle source <-> preview (formatted) mode
//                     (S/s and P/p are left to Lister: ASCII charset / previous file)
//   F1, ?             help page (the embedded README.md); F1/?/Esc return to the file
//   F2                reload the file from disk (keeps mode and position)
//   Up/Down           scroll one line
//   PgUp/PgDn         scroll one page
//   Home/End          top / bottom
//   Left/Right        horizontal scroll (source mode without wrapping)
//   Ctrl+C/Ctrl+Ins   copy selection,  Ctrl+A select all
//   F7, Alt+F7        -> Lister's "Find" dialog (Lister then calls ListSearchText); Ctrl+F is Lister's own
//   F3, Shift+F3      -> Lister's "Find next/previous"
//   Esc               -> Lister: close the window
//   all other keys    -> posted to the Lister window (N, P, S, 1..7, ...)
//                        (Lister itself does not reload plugin content on F2, hence our own F2)
//
// Auto-reload (AutoReload=1, default): the file's time stamp/size is polled; a change that
// is stable for one polling interval reloads the file, keeping mode and scroll position.

#include "lister.h"
#include "listplug.h"
#include "md_view.h"
#include "text_load.h"
#include "resource.h"
#include <shellapi.h>
#include <vector>
#include <cstdlib>
#include <cwctype>
#include <cwchar>

namespace {

const wchar_t kClassName[] = L"MarkdownViewListerWnd";
const wchar_t kIniSection[] = L"MarkdownView";
const UINT_PTR kResizeTimer = 1;
const UINT_PTR kAutoReloadTimer = 2;
const UINT_PTR kRetryTimer = 3;
const UINT kAutoReloadIntervalMs = 700;

HINSTANCE g_hinst = NULL;
bool g_classRegistered = false;
std::wstring g_iniFile;

enum class StartMode { Last, Preview, Source };

struct Settings {
    StartMode startMode = StartMode::Last;
    Backend backend = Backend::RichEdit;
    std::wstring bodyFont = L"Segoe UI";
    std::wstring codeFont = L"Consolas";
    int fontSize = 11;
    int codeFontSize = 10;
    unsigned long maxFileSize = 16 * 1024 * 1024;
    bool autoReload = true;
    size_t previewMaxChars = 4 * 1024 * 1024;   // larger files open in source mode
    std::vector<std::wstring> extensions;   // lower case, without dot
};
Settings g_settings;
bool g_settingsLoaded = false;
ViewMode g_lastMode = ViewMode::Preview;

std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

std::wstring IniString(const wchar_t* key, const wchar_t* def) {
    if (g_iniFile.empty()) return def;
    wchar_t buf[1024];
    GetPrivateProfileStringW(kIniSection, key, def, buf, 1024, g_iniFile.c_str());
    return buf;
}
int IniInt(const wchar_t* key, int def) {
    if (g_iniFile.empty()) return def;
    return (int)GetPrivateProfileIntW(kIniSection, key, def, g_iniFile.c_str());
}

void SetDefaultExtensions() {
    static const wchar_t* exts[] = { L"md", L"markdown", L"mdown", L"mkd", L"mkdn", L"mdwn", L"mdtxt", L"mdtext", L"rmd", 0 };
    g_settings.extensions.clear();
    for (int i = 0; exts[i]; i++) g_settings.extensions.push_back(exts[i]);
}

void LoadSettings() {
    g_settingsLoaded = true;
    SetDefaultExtensions();
    std::wstring sm = Lower(IniString(L"StartMode", L"last"));
    g_settings.startMode = sm == L"preview" ? StartMode::Preview : sm == L"source" ? StartMode::Source : StartMode::Last;
    std::wstring last = Lower(IniString(L"LastMode", L"preview"));
    g_lastMode = last == L"source" ? ViewMode::Source : ViewMode::Preview;
    g_settings.backend = Backend::RichEdit;   // Renderer=richedit (only back-end for now)
    g_settings.bodyFont = IniString(L"FontName", L"Segoe UI");
    g_settings.codeFont = IniString(L"CodeFontName", L"Consolas");
    g_settings.fontSize = IniInt(L"FontSize", 11);
    g_settings.codeFontSize = IniInt(L"CodeFontSize", 10);
    if (g_settings.fontSize < 6 || g_settings.fontSize > 48) g_settings.fontSize = 11;
    if (g_settings.codeFontSize < 6 || g_settings.codeFontSize > 48) g_settings.codeFontSize = 10;
    int mb = IniInt(L"MaxFileSizeMB", 16);
    if (mb < 1) mb = 1;
    if (mb > 512) mb = 512;
    g_settings.maxFileSize = (unsigned long)mb * 1024 * 1024;
    g_settings.autoReload = IniInt(L"AutoReload", 1) != 0;
    int pmb = IniInt(L"PreviewMaxSizeMB", 4);
    if (pmb < 1) pmb = 1;
    g_settings.previewMaxChars = (size_t)pmb * 1024 * 1024;
    std::wstring ext = Lower(IniString(L"Extensions", L""));
    if (!ext.empty()) {
        std::vector<std::wstring> list;
        std::wstring cur;
        for (size_t i = 0; i <= ext.size(); i++) {
            wchar_t c = i < ext.size() ? ext[i] : L',';
            if (c == L',' || c == L';' || c == L' ') {
                if (!cur.empty() && cur[0] == L'.') cur = cur.substr(1);
                if (!cur.empty()) list.push_back(cur);
                cur.clear();
            } else cur += c;
        }
        if (!list.empty()) g_settings.extensions = list;
    }
}

void EnsureSettings() {
    if (!g_settingsLoaded) LoadSettings();
}

void SaveLastMode(ViewMode m) {
    g_lastMode = m;
    if (!g_iniFile.empty() && g_settings.startMode == StartMode::Last)
        WritePrivateProfileStringW(kIniSection, L"LastMode", m == ViewMode::Source ? L"source" : L"preview", g_iniFile.c_str());
}

IMarkdownView* CreateView(Backend b) {
    switch (b) {
    case Backend::RichEdit:
    default:
        return CreateRichEditView();
    }
}

std::wstring PercentDecode(const std::wstring& s) {
    std::string bytes;
    bool any = false;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'%' && i + 2 < s.size() && iswxdigit(s[i + 1]) && iswxdigit(s[i + 2])) {
            wchar_t h[3] = { s[i + 1], s[i + 2], 0 };
            bytes += (char)wcstol(h, NULL, 16);
            i += 2;
            any = true;
        } else if (s[i] < 0x80) {
            bytes += (char)s[i];
        } else {
            // non-ASCII: encode as UTF-8 so that decoding below keeps it
            char u[8];
            int n = WideCharToMultiByte(CP_UTF8, 0, &s[i], 1, u, sizeof(u), NULL, NULL);
            bytes.append(u, n > 0 ? n : 0);
        }
    }
    if (!any) return s;
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), NULL, 0);
    std::wstring r(n > 0 ? n : 0, 0);
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &r[0], n);
    return r;
}

bool IsDangerousFile(const std::wstring& path) {
    static const wchar_t* bad[] = { L".exe", L".com", L".bat", L".cmd", L".scr", L".pif", L".vbs", L".vbe", L".js",
        L".jse", L".wsf", L".wsh", L".ps1", L".msi", L".msp", L".lnk", L".hta", L".cpl", L".jar", L".reg", L".url",
        L".dll", L".sys", L".inf", L".application", L".appref-ms", 0 };
    std::wstring l = Lower(path);
    size_t dot = l.find_last_of(L'.');
    size_t sl = l.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sl != std::wstring::npos && dot < sl)) return false;
    std::wstring ext = l.substr(dot);
    for (int i = 0; bad[i]; i++) if (ext == bad[i]) return true;
    return false;
}

// Help text: README.md embedded as RCDATA resource (UTF-8)
std::wstring LoadHelpText() {
    HRSRC r = FindResourceW(g_hinst, MAKEINTRESOURCEW(IDR_HELP_MD), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (r) {
        HGLOBAL g = LoadResource(g_hinst, r);
        DWORD size = SizeofResource(g_hinst, r);
        const char* p = g ? (const char*)LockResource(g) : NULL;
        if (p && size) {
            if (size >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) { p += 3; size -= 3; }
            int n = MultiByteToWideChar(CP_UTF8, 0, p, (int)size, NULL, 0);
            if (n > 0) {
                std::wstring w(n, 0);
                MultiByteToWideChar(CP_UTF8, 0, p, (int)size, &w[0], n);
                return w;
            }
        }
    }
    return L"# TcWlxMarkdownViewAir\n\n"
           L"| Key | Action |\n|---|---|\n"
           L"| **Ctrl+M**, **Ctrl+Tab**, **Ctrl+U** | Toggle source / preview mode |\n"
           L"| **F1**, **?** | This help (press again or Esc to return) |\n"
           L"| **F7**, **Alt+F7**, **Ctrl+F** | Find |\n"
           L"| **F3** / **Shift+F3** | Find next / previous |\n"
           L"| **F2** | Reload the file |\n"
           L"| **Esc** | Close |\n";
}

// Character a WM_KEYDOWN would produce with the current keyboard layout and modifier state
wchar_t KeyToChar(WPARAM vk, LPARAM lp) {
    BYTE state[256];
    if (!GetKeyboardState(state)) return 0;
    wchar_t buf[8];
    UINT scan = (UINT)((lp >> 16) & 0xFF);
    if (!scan) scan = MapVirtualKeyW((UINT)vk, 0 /*MAPVK_VK_TO_VSC*/);
    // flag 4: do not change the keyboard state (Windows 10 1607+; ignored before)
    int n = ToUnicodeEx((UINT)vk, scan, state, buf, 8, 4, GetKeyboardLayout(0));
    return n == 1 ? buf[0] : 0;
}

struct FileStamp {
    bool valid = false;
    FILETIME time = { 0, 0 };
    ULONGLONG size = 0;
    bool operator==(const FileStamp& o) const {
        return valid == o.valid && time.dwLowDateTime == o.time.dwLowDateTime &&
               time.dwHighDateTime == o.time.dwHighDateTime && size == o.size;
    }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};

bool GetStamp(const std::wstring& path, FileStamp& st) {
    st = FileStamp();
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    st.valid = true;
    st.time = fad.ftLastWriteTime;
    st.size = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    return true;
}

bool ReadDocument(const std::wstring& file, DocumentData& d) {
    std::wstring text;
    if (!LoadTextFile(file, text, g_settings.maxFileSize)) return false;
    d.path = file;
    size_t sl = file.find_last_of(L"\\/");
    d.baseDir = sl == std::wstring::npos ? std::wstring() : file.substr(0, sl + 1);
    d.source.swap(text);
    d.doc = md::Parse(d.source);
    return true;
}

class MarkdownLister : public IViewHost {
public:
    HWND hwnd_ = NULL;
    IMarkdownView* view_ = nullptr;
    DocumentData data_;
    ViewMode mode_ = ViewMode::Preview;
    int flags_ = 0;
    int renderedWidth_ = 0;
    // help page state: the real document is parked while the help is shown
    bool help_ = false;
    DocumentData parkedData_;
    ViewMode parkedMode_ = ViewMode::Preview;
    int parkedPercent_ = 0;
    // reload / auto-reload state
    FileStamp loadedStamp_;
    FileStamp pendingStamp_;
    int retriesLeft_ = 0;

    ~MarkdownLister() override { delete view_; }

    ViewSettings CurrentViewSettings() const {
        ViewSettings vs;
        vs.bodyFont = g_settings.bodyFont;
        vs.codeFont = g_settings.codeFont;
        vs.fontSize = g_settings.fontSize;
        vs.codeFontSize = g_settings.codeFontSize;
        vs.wrapSource = (flags_ & lcp_wraptext) != 0;
        vs.dark = (flags_ & lcp_darkmode) != 0;
        return vs;
    }

    bool Load(const std::wstring& file, int flags) {
        FileStamp st;
        GetStamp(file, st);
        DocumentData d;
        if (!ReadDocument(file, d)) return false;
        flags_ = flags;
        if (help_) { help_ = false; parkedData_ = DocumentData(); mode_ = parkedMode_; }
        data_ = d;
        loadedStamp_ = st;
        pendingStamp_ = FileStamp();
        retriesLeft_ = 0;
        KillTimer(hwnd_, kRetryTimer);
        // very large files: rich preview would be slow, start in source mode (P still switches)
        if (data_.source.size() > g_settings.previewMaxChars) mode_ = ViewMode::Source;
        Render();
        NotifyPercent();
        return true;
    }

    void Render() {
        view_->Show(data_, mode_, CurrentViewSettings());
        RECT r; GetClientRect(hwnd_, &r);
        renderedWidth_ = r.right;
    }

    void Rerender() {
        int pct = view_->GetPercent();
        Render();
        view_->SetPercent(pct);
        NotifyPercent();
    }

    void SetMode(ViewMode m) {
        if (m == mode_) return;
        mode_ = m;
        if (!help_) SaveLastMode(m);
        Rerender();
    }

    void ToggleMode() {
        SetMode(mode_ == ViewMode::Preview ? ViewMode::Source : ViewMode::Preview);
    }

    void ShowHelp() {
        if (help_) { HideHelp(); return; }
        parkedPercent_ = view_->GetPercent();
        parkedMode_ = mode_;
        parkedData_ = data_;
        data_ = DocumentData();
        data_.source = LoadHelpText();
        wchar_t dll[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(g_hinst, dll, MAX_PATH * 2);
        std::wstring dir(dll, n);
        size_t sl = dir.find_last_of(L'\\');
        data_.baseDir = sl == std::wstring::npos ? std::wstring() : dir.substr(0, sl + 1);
        data_.path = data_.baseDir + L"README.md";
        data_.doc = md::Parse(data_.source);
        help_ = true;
        mode_ = ViewMode::Preview;
        Render();
        NotifyPercent();
    }

    void HideHelp() {
        if (!help_) return;
        help_ = false;
        data_ = parkedData_;
        parkedData_ = DocumentData();
        mode_ = parkedMode_;
        Render();
        view_->SetPercent(parkedPercent_);
        NotifyPercent();
    }

    const std::wstring& DocumentPath() const { return help_ ? parkedData_.path : data_.path; }

    // Re-reads the document from disk, keeping mode and scroll position.
    // While the help page is shown, the parked document is refreshed silently.
    bool ReloadNow() {
        const std::wstring path = DocumentPath();
        if (path.empty()) return true;
        FileStamp st;
        GetStamp(path, st);
        DocumentData d;
        if (!ReadDocument(path, d)) return false;      // e.g. still locked by the editor
        loadedStamp_ = st;
        pendingStamp_ = FileStamp();
        if (help_) { parkedData_ = d; return true; }
        int pos = view_->GetScrollPosition();
        data_ = d;
        Render();
        view_->SetScrollPosition(pos);
        NotifyPercent();
        return true;
    }

    void ManualReload() {
        if (ReloadNow()) { retriesLeft_ = 0; KillTimer(hwnd_, kRetryTimer); return; }
        retriesLeft_ = 8;                               // retry for ~2 s, then give up
        SetTimer(hwnd_, kRetryTimer, 250, NULL);
    }

    void OnRetryTimer() {
        if (ReloadNow()) { retriesLeft_ = 0; KillTimer(hwnd_, kRetryTimer); return; }
        if (--retriesLeft_ <= 0) { KillTimer(hwnd_, kRetryTimer); MessageBeep(MB_ICONWARNING); }
    }

    void OnAutoReloadTimer() {
        FileStamp st;
        if (!GetStamp(DocumentPath(), st)) return;      // missing for a moment (safe-save rename)
        if (st == loadedStamp_) { pendingStamp_ = FileStamp(); return; }
        if (pendingStamp_.valid && st == pendingStamp_) ReloadNow();   // unchanged for one interval
        else pendingStamp_ = st;
    }

    HWND ListerWindow() const { return GetParent(hwnd_); }

    void NotifyPercent() {
        HWND p = ListerWindow();
        if (p) PostMessageW(p, WM_COMMAND, MAKELONG(view_->GetPercent(), itm_percent), (LPARAM)hwnd_);
    }

    // Unhandled keys go to the Lister window (verified to work for Esc, N, Ctrl+F ...)
    void PassToLister(UINT msg, WPARAM wp, LPARAM lp) {
        HWND p = ListerWindow();
        if (p) PostMessageW(p, msg, wp, lp);
    }

    // Keys handled by the plugin itself. Returns true if handled.
    bool HandleOwnKey(WPARAM wp, wchar_t ch, bool ctrl, bool shift, bool alt) {
        if (ch == L'?' || (wp == VK_F1 && !ctrl && !alt && !shift)) { ShowHelp(); return true; }
        if (wp == VK_ESCAPE && help_ && !ctrl && !alt && !shift) { HideHelp(); return true; }
        switch (wp) {
        case VK_UP:    view_->Scroll(ScrollCmd::LineUp); return true;
        case VK_DOWN:  view_->Scroll(ScrollCmd::LineDown); return true;
        case VK_PRIOR: view_->Scroll(ScrollCmd::PageUp); return true;
        case VK_NEXT:  view_->Scroll(ScrollCmd::PageDown); return true;
        case VK_HOME:  view_->Scroll(ScrollCmd::Top); return true;
        case VK_END:   view_->Scroll(ScrollCmd::Bottom); return true;
        case VK_LEFT:  view_->Scroll(ScrollCmd::Left); return true;
        case VK_RIGHT: view_->Scroll(ScrollCmd::Right); return true;
        case VK_F2:
            if (ctrl || alt || shift) return false;
            ManualReload();
            return true;
        case 'M':       // Ctrl+M: "Markdown / Mode"
        case 'U':       // Ctrl+U: "view source" as in web browsers
        case VK_TAB:    // Ctrl+Tab / Ctrl+Shift+Tab: switch view
            if (!ctrl || alt) return false;
            ToggleMode();
            return true;
        case 'C':
        case VK_INSERT:
            if (!ctrl || alt) return false;
            view_->Copy();
            return true;
        case 'A':
            if (!ctrl || alt) return false;
            view_->SelectAll();
            return true;
        }
        return false;
    }

    // ---- IViewHost
    bool OnViewKey(UINT msg, WPARAM wp, LPARAM lp) override {
        if (msg == WM_CHAR) return true;          // read-only: swallow characters (no beeps)
        if (msg == WM_SYSCHAR) return false;      // Alt+letter -> menu handling by Windows

        if (msg == WM_SYSKEYDOWN) {
            if (wp == VK_F7) {                    // Alt+F7 -> search, as F7
                PassToLister(WM_KEYDOWN, VK_F7, lp & ~(1 << 29));
                return true;
            }
            return false;
        }
        if (msg != WM_KEYDOWN) return false;

        switch (wp) {
        case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN: case VK_APPS:
            return false;
        }
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        bool shift = GetKeyState(VK_SHIFT) < 0;
        bool alt = GetKeyState(VK_MENU) < 0;
        // layout-independent character (? is Shift+/ on US, Shift+, on Hungarian keyboards ...)
        wchar_t ch = KeyToChar(wp, lp);
        if (HandleOwnKey(wp, ch, ctrl, shift, alt)) return true;
        // Esc, F3, F7, Ctrl+F, N, P, S, 1..7 ... are Lister's business
        PassToLister(WM_KEYDOWN, wp, lp);
        return true;
    }

    void OnViewScrolled() override { NotifyPercent(); }

    void OnLinkClicked(const std::wstring& urlIn) override {
        std::wstring url = urlIn;
        while (!url.empty() && iswspace(url[0])) url.erase(0, 1);
        if (url.empty()) return;
        if (url[0] == L'#') {
            if (!view_->ScrollToAnchor(PercentDecode(url.substr(1)))) MessageBeep(MB_ICONWARNING);
            return;
        }
        std::wstring low = Lower(url);
        if (low.compare(0, 7, L"http://") == 0 || low.compare(0, 8, L"https://") == 0 ||
            low.compare(0, 7, L"mailto:") == 0 || low.compare(0, 6, L"ftp://") == 0) {
            ShellExecuteW(hwnd_, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            return;
        }
        if (low.compare(0, 4, L"www.") == 0) {
            std::wstring u = L"http://" + url;
            ShellExecuteW(hwnd_, L"open", u.c_str(), NULL, NULL, SW_SHOWNORMAL);
            return;
        }
        // local file: file:///C:/x or relative path (anchors / queries dropped)
        std::wstring path = url;
        if (low.compare(0, 8, L"file:///") == 0) path = url.substr(8);
        else if (low.compare(0, 7, L"file://") == 0) path = L"\\\\" + url.substr(7);
        else if (low.find(L':') != std::wstring::npos && !(low.size() > 2 && low[1] == L':')) {
            MessageBeep(MB_ICONWARNING);   // unknown scheme
            return;
        }
        size_t cut = path.find_first_of(L"#?");
        if (cut != std::wstring::npos) path = path.substr(0, cut);
        path = PercentDecode(path);
        for (size_t i = 0; i < path.size(); i++) if (path[i] == L'/') path[i] = L'\\';
        if (path.empty()) return;
        bool absolute = (path.size() > 1 && path[1] == L':') || (path.size() > 1 && path[0] == L'\\' && path[1] == L'\\');
        if (!absolute) {
            if (path[0] == L'\\') path = path.substr(1);
            path = data_.baseDir + path;
        }
        wchar_t full[MAX_PATH * 4];
        DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH * 4, full, NULL);
        if (n == 0 || n >= MAX_PATH * 4) { MessageBeep(MB_ICONWARNING); return; }
        DWORD attr = GetFileAttributesW(full);
        if (attr == INVALID_FILE_ATTRIBUTES || IsDangerousFile(full)) { MessageBeep(MB_ICONWARNING); return; }
        if (attr & FILE_ATTRIBUTE_DIRECTORY) ShellExecuteW(hwnd_, L"explore", full, NULL, NULL, SW_SHOWNORMAL);
        else ShellExecuteW(hwnd_, L"open", full, NULL, NULL, SW_SHOWNORMAL);
    }

    // ---- window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        MarkdownLister* self = (MarkdownLister*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
            self = (MarkdownLister*)cs->lpCreateParams;
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            break;
        }
        case WM_CREATE:
            self->view_ = CreateView(g_settings.backend);
            if (!self->view_ || !self->view_->Create(hwnd, self)) return -1;
            if (g_settings.autoReload) SetTimer(hwnd, kAutoReloadTimer, kAutoReloadIntervalMs, NULL);
            return 0;
        case WM_SIZE:
            if (self && self->view_) {
                self->view_->Resize(LOWORD(lp), HIWORD(lp));
                int w = LOWORD(lp);
                // preview layout (rules, code block widths) depends on the width: re-render when it changes
                if (self->mode_ == ViewMode::Preview && self->renderedWidth_ > 0 && w > 0 &&
                    abs(w - self->renderedWidth_) * 100 / self->renderedWidth_ >= 6)
                    SetTimer(hwnd, kResizeTimer, 300, NULL);
            }
            return 0;
        case WM_TIMER:
            if (wp == kResizeTimer) {
                KillTimer(hwnd, kResizeTimer);
                if (self && self->view_ && !self->data_.path.empty()) self->Rerender();
                return 0;
            }
            if (wp == kAutoReloadTimer) {
                if (self && self->view_) self->OnAutoReloadTimer();
                return 0;
            }
            if (wp == kRetryTimer) {
                if (self && self->view_) self->OnRetryTimer();
                return 0;
            }
            break;
        case WM_SETFOCUS:
            if (self && self->view_) self->view_->Focus();
            return 0;
        case WM_NOTIFY:
        case WM_COMMAND:
            if (self && self->view_) {
                LRESULT r = 0;
                if (self->view_->OnParentNotify(msg, wp, lp, r)) return r;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete self;
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
};

MarkdownLister* FromHwnd(HWND h) {
    if (!h || !IsWindow(h)) return nullptr;
    wchar_t cls[64];
    if (!GetClassNameW(h, cls, 64) || lstrcmpW(cls, kClassName) != 0) return nullptr;
    return (MarkdownLister*)GetWindowLongPtrW(h, GWLP_USERDATA);
}

bool RegisterClassOnce() {
    if (g_classRegistered) return true;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MarkdownLister::WndProc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    g_classRegistered = true;
    return true;
}

} // namespace

// ---------------------------------------------------------------- API used by listplug.cpp

void Lister_Init(HINSTANCE hinst) {
    g_hinst = hinst;   // nothing else here: called from DllMain
}

void Lister_SetIniFile(const std::wstring& iniFile) {
    g_iniFile = iniFile;
    LoadSettings();
}

bool Lister_IsSupportedFile(const wchar_t* fileName) {
    EnsureSettings();
    if (!fileName) return false;
    const wchar_t* sl = wcsrchr(fileName, L'\\');
    const wchar_t* dot = wcsrchr(sl ? sl : fileName, L'.');
    if (!dot) return false;
    std::wstring ext = Lower(dot + 1);
    for (size_t i = 0; i < g_settings.extensions.size(); i++)
        if (g_settings.extensions[i] == ext) return true;
    return false;
}

std::string Lister_DetectString() {
    EnsureSettings();
    std::string s;
    for (size_t i = 0; i < g_settings.extensions.size(); i++) {
        if (i) s += " | ";
        s += "EXT=\"";
        const std::wstring& e = g_settings.extensions[i];
        for (size_t k = 0; k < e.size(); k++) {
            wchar_t c = e[k];
            if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
            s += (c < 0x80 && c != L'"') ? (char)c : '_';
        }
        s += "\"";
    }
    return s;
}

HWND Lister_Open(HWND parent, const wchar_t* fileName, int showFlags) {
    EnsureSettings();
    if (!RegisterClassOnce()) return NULL;
    MarkdownLister* self = new MarkdownLister();
    switch (g_settings.startMode) {
    case StartMode::Preview: self->mode_ = ViewMode::Preview; break;
    case StartMode::Source:  self->mode_ = ViewMode::Source; break;
    default:                 self->mode_ = g_lastMode; break;
    }
    RECT r; GetClientRect(parent, &r);
    // created invisible; shown once the content is loaded
    HWND h = CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, L"", WS_CHILD | WS_CLIPCHILDREN,
                             r.left, r.top, r.right - r.left, r.bottom - r.top, parent, NULL, g_hinst, self);
    if (!h) return NULL;   // WM_NCDESTROY deleted self if creation failed after WM_NCCREATE
    if (!self->Load(fileName, showFlags)) {
        DestroyWindow(h);
        return NULL;
    }
    ShowWindow(h, SW_SHOW);
    return h;
}

bool Lister_LoadNext(HWND listWin, const wchar_t* fileName, int showFlags) {
    MarkdownLister* self = FromHwnd(listWin);
    return self && self->Load(fileName, showFlags);
}

int Lister_SendCommand(HWND listWin, int command, int parameter) {
    MarkdownLister* self = FromHwnd(listWin);
    if (!self) return LISTPLUGIN_ERROR;
    switch (command) {
    case lc_copy:
        self->view_->Copy();
        return LISTPLUGIN_OK;
    case lc_selectall:
        self->view_->SelectAll();
        return LISTPLUGIN_OK;
    case lc_setpercent:
        self->view_->SetPercent(parameter);
        self->NotifyPercent();
        return LISTPLUGIN_OK;
    case lc_newparams: {
        int old = self->flags_;
        self->flags_ = parameter;
        if ((old ^ parameter) & (lcp_darkmode | (self->mode_ == ViewMode::Source ? lcp_wraptext : 0)))
            self->Rerender();
        return LISTPLUGIN_OK;
    }
    }
    return LISTPLUGIN_ERROR;
}

int Lister_SearchText(HWND listWin, const wchar_t* text, int searchParameter) {
    MarkdownLister* self = FromHwnd(listWin);
    if (!self || !text) return LISTPLUGIN_ERROR;
    if (self->view_->Find(text, searchParameter)) {
        self->NotifyPercent();
        return LISTPLUGIN_OK;
    }
    return LISTPLUGIN_ERROR;
}

int Lister_Print(HWND listWin, const wchar_t* fileName, const RECT* margins) {
    MarkdownLister* self = FromHwnd(listWin);
    if (!self) return LISTPLUGIN_ERROR;
    return self->view_->Print(fileName ? fileName : self->data_.path.c_str(), margins);
}
