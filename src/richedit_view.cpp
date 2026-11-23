// richedit_view.cpp - IMarkdownView back-end based on the RichEdit control (Msftedit.dll)
//
// Preview mode: Markdown -> RTF (md_rtf.cpp) streamed into a read-only RichEdit.
// Source mode : the Markdown text in a monospaced font (also via RTF).

#include "md_view.h"
#include "md_rtf.h"
#include <richedit.h>
#include <commdlg.h>
#include <algorithm>
#include <vector>

#include "listplug.h"

#ifndef MSFTEDIT_CLASS
#define MSFTEDIT_CLASS L"RICHEDIT50W"
#endif

namespace {

HMODULE g_richEditLib = NULL;
const wchar_t* g_richEditClass = NULL;

bool EnsureRichEdit() {
    if (g_richEditClass) return true;
    UINT old = SetErrorMode(SEM_NOOPENFILEERRORBOX);
    g_richEditLib = LoadLibraryW(L"Msftedit.dll");
    if (g_richEditLib) g_richEditClass = MSFTEDIT_CLASS;
    else {
        g_richEditLib = LoadLibraryW(L"Riched20.dll");
        if (g_richEditLib) g_richEditClass = L"RichEdit20W";
    }
    SetErrorMode(old);
    return g_richEditClass != NULL;
}

struct StreamCookie { const std::string* data; size_t pos; };

DWORD CALLBACK StreamInCallback(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb) {
    StreamCookie* c = (StreamCookie*)cookie;
    size_t left = c->data->size() - c->pos;
    size_t n = std::min(left, (size_t)cb);
    memcpy(buf, c->data->data() + c->pos, n);
    c->pos += n;
    *pcb = (LONG)n;
    return 0;
}

const wchar_t kPropSelf[] = L"MarkdownView.Self";

class RichEditView : public IMarkdownView {
public:
    RichEditView() {}
    ~RichEditView() override {
        if (edit_ && IsWindow(edit_)) {
            Unsubclass(edit_);
            DestroyWindow(edit_);
        }
    }

    bool Create(HWND parent, IViewHost* host) override {
        if (!EnsureRichEdit()) return false;
        host_ = host;
        RECT r; GetClientRect(parent, &r);
        edit_ = CreateWindowExW(0, g_richEditClass, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL,
            0, 0, r.right, r.bottom, parent, (HMENU)1, (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
        if (!edit_) return false;
        SendMessageW(edit_, EM_EXLIMITTEXT, 0, 0x7FFFFFFF);
        SendMessageW(edit_, EM_SETUNDOLIMIT, 0, 0);
        SendMessageW(edit_, EM_SETEVENTMASK, 0, ENM_LINK);
        SendMessageW(edit_, EM_AUTOURLDETECT, FALSE, 0);
#ifndef SES_HYPERLINKTOOLTIPS
#define SES_HYPERLINKTOOLTIPS 8
#endif
        // RichEdit 8+: show the link target as tooltip
        SendMessageW(edit_, EM_SETEDITSTYLE, SES_HYPERLINKTOOLTIPS, SES_HYPERLINKTOOLTIPS);
        SetPropW(edit_, kPropSelf, (HANDLE)this);
        oldProc_ = (WNDPROC)SetWindowLongPtrW(edit_, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
        UpdateFormatRect();
        return true;
    }

    HWND Window() const override { return edit_; }

    void Show(const DocumentData& data, ViewMode mode, const ViewSettings& s) override {
        mode_ = mode;
        md::RtfStyle st;
        st.bodyFont = s.bodyFont;
        st.codeFont = s.codeFont;
        st.baseSize = s.fontSize;
        st.codeSize = s.codeFontSize;
        st.dark = s.dark;
        st.pageWidthTwips = TextWidthTwips();

        std::string rtf;
        if (mode == ViewMode::Preview) {
            md::RtfOutput out = md::RenderRtf(data.doc, st);
            rtf.swap(out.rtf);
            headings_.swap(out.headings);
            links_.swap(out.links);
        } else {
            rtf = md::RenderSourceRtf(data.source, st);
            headings_.clear();
            links_.clear();
        }

        SendMessageW(edit_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(edit_, EM_SETBKGNDCOLOR, 0, (LPARAM)md::RtfBackground(s.dark));
        StreamCookie ck = { &rtf, 0 };
        EDITSTREAM es = { (DWORD_PTR)&ck, 0, StreamInCallback };
        SendMessageW(edit_, EM_STREAMIN, SF_RTF, (LPARAM)&es);
        bool wrap = mode == ViewMode::Preview || s.wrapSource;
        SendMessageW(edit_, EM_SETTARGETDEVICE, 0, wrap ? 0 : 1);
        UpdateFormatRect();
        SendMessageW(edit_, EM_SETSEL, 0, 0);
        SendMessageW(edit_, WM_VSCROLL, SB_TOP, 0);
        SendMessageW(edit_, WM_HSCROLL, SB_LEFT, 0);
        SendMessageW(edit_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(edit_, NULL, TRUE);
        if (GetFocus() == edit_) HideCaret(edit_);
    }

    void Resize(int cx, int cy) override {
        MoveWindow(edit_, 0, 0, cx, cy, TRUE);
        UpdateFormatRect();
    }

    void Focus() override { SetFocus(edit_); }

    void Scroll(ScrollCmd cmd) override {
        switch (cmd) {
        case ScrollCmd::LineUp:   SendMessageW(edit_, WM_VSCROLL, SB_LINEUP, 0); break;
        case ScrollCmd::LineDown: SendMessageW(edit_, WM_VSCROLL, SB_LINEDOWN, 0); break;
        case ScrollCmd::PageUp:   SendMessageW(edit_, WM_VSCROLL, SB_PAGEUP, 0); break;
        case ScrollCmd::PageDown: SendMessageW(edit_, WM_VSCROLL, SB_PAGEDOWN, 0); break;
        case ScrollCmd::Top:      SendMessageW(edit_, WM_VSCROLL, SB_TOP, 0); break;
        case ScrollCmd::Bottom:   SendMessageW(edit_, WM_VSCROLL, SB_BOTTOM, 0); break;
        case ScrollCmd::Left:     SendMessageW(edit_, WM_HSCROLL, SB_LINELEFT, 0); break;
        case ScrollCmd::Right:    SendMessageW(edit_, WM_HSCROLL, SB_LINERIGHT, 0); break;
        }
    }

    int GetPercent() override {
        SCROLLINFO si; memset(&si, 0, sizeof(si)); si.cbSize = sizeof(si); si.fMask = SIF_ALL;
        if (!GetScrollInfo(edit_, SB_VERT, &si)) return 0;
        int range = si.nMax - si.nMin - (int)si.nPage + 1;
        if (range <= 0) return 100;
        int pos = si.nPos - si.nMin;
        if (pos >= range) return 100;
        return std::max(0, std::min(100, MulDiv(pos, 100, range)));
    }

    void SetPercent(int percent) override {
        if (percent <= 0) { SendMessageW(edit_, WM_VSCROLL, SB_TOP, 0); return; }
        if (percent >= 100) { SendMessageW(edit_, WM_VSCROLL, SB_BOTTOM, 0); return; }
        int lines = (int)SendMessageW(edit_, EM_GETLINECOUNT, 0, 0);
        int first = (int)SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0);
        int target = MulDiv(percent, lines, 100);
        SendMessageW(edit_, EM_LINESCROLL, 0, target - first);
    }

    int GetScrollPosition() override {
        return (int)SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0);
    }

    void SetScrollPosition(int line) override {
        int first = (int)SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0);
        SendMessageW(edit_, EM_LINESCROLL, 0, line - first);
    }

    bool Find(const std::wstring& text, int flags) override {
        if (text.empty()) return false;
        CHARRANGE sel;
        SendMessageW(edit_, EM_EXGETSEL, 0, (LPARAM)&sel);
        LONG len = (LONG)SendMessageW(edit_, WM_GETTEXTLENGTH, 0, 0);
        bool backwards = (flags & lcs_backwards) != 0;
        LONG start;
        if (flags & lcs_findfirst) {
            if (backwards) start = (sel.cpMin != sel.cpMax) ? sel.cpMin : len;
            else start = FirstVisibleChar();
        } else {
            start = backwards ? sel.cpMin : sel.cpMin + 1;
        }
        FINDTEXTEXW ft;
        ft.chrg.cpMin = start;
        ft.chrg.cpMax = backwards ? 0 : -1;
        ft.lpstrText = text.c_str();
        WPARAM fl = 0;
        if (!backwards) fl |= FR_DOWN;
        if (flags & lcs_matchcase) fl |= FR_MATCHCASE;
        if (flags & lcs_wholewords) fl |= FR_WHOLEWORD;
        LONG idx = (LONG)SendMessageW(edit_, EM_FINDTEXTEXW, fl, (LPARAM)&ft);
        if (idx < 0) return false;
        SendMessageW(edit_, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText);
        EnsureVisible(ft.chrgText.cpMin);
        if (GetFocus() == edit_) HideCaret(edit_);
        return true;
    }

    void Copy() override { SendMessageW(edit_, WM_COPY, 0, 0); }
    void SelectAll() override { SendMessageW(edit_, EM_SETSEL, 0, -1); }

    int Print(const std::wstring& title, const RECT* margins) override;

    bool ScrollToAnchor(const std::wstring& anchor) override {
        std::wstring a = Lower(anchor);
        // the n-th heading is found by searching all headings in document order
        LONG pos = 0;
        for (size_t i = 0; i < headings_.size(); i++) {
            const std::wstring& t = headings_[i].text;
            if (t.empty()) continue;
            FINDTEXTEXW ft;
            ft.chrg.cpMin = pos; ft.chrg.cpMax = -1;
            ft.lpstrText = t.c_str();
            LONG idx = (LONG)SendMessageW(edit_, EM_FINDTEXTEXW, FR_DOWN | FR_MATCHCASE, (LPARAM)&ft);
            if (idx < 0) continue;
            pos = ft.chrgText.cpMax;
            if (Lower(headings_[i].anchor) == a) {
                int line = (int)SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, idx);
                int first = (int)SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0);
                SendMessageW(edit_, EM_LINESCROLL, 0, line - first);
                CHARRANGE cr = { idx, idx };
                SendMessageW(edit_, EM_EXSETSEL, 0, (LPARAM)&cr);
                if (host_) host_->OnViewScrolled();
                return true;
            }
        }
        return false;
    }

    bool OnParentNotify(UINT msg, WPARAM, LPARAM lp, LRESULT& result) override {
        if (msg != WM_NOTIFY) return false;
        NMHDR* nh = (NMHDR*)lp;
        if (nh->hwndFrom != edit_ || nh->code != EN_LINK) return false;
        ENLINK* el = (ENLINK*)lp;
        if (el->msg == WM_LBUTTONDOWN) {
            linkDownSel_ = CurrentSel();
            result = 0;
            return true;
        }
        if (el->msg == WM_LBUTTONUP) {
            CHARRANGE s = CurrentSel();
            // ignore if the user was dragging a selection
            if (s.cpMin == s.cpMax || (s.cpMin == linkDownSel_.cpMin && s.cpMax == linkDownSel_.cpMax)) {
                std::wstring url = ResolveLinkUrl(el->chrg);
                if (!url.empty() && host_) host_->OnLinkClicked(url);
            }
            result = 0;
            return true;
        }
        return false;
    }

private:
    HWND edit_ = NULL;
    IViewHost* host_ = nullptr;
    ViewMode mode_ = ViewMode::Preview;
    std::vector<md::HeadingRef> headings_;
    std::vector<md::LinkRef> links_;
    CHARRANGE linkDownSel_ = { 0, 0 };
    WNDPROC oldProc_ = NULL;

    void Unsubclass(HWND hwnd) {
        if (oldProc_ && (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC) == SubclassProc)
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)oldProc_);
        RemovePropW(hwnd, kPropSelf);
    }

    static std::wstring Lower(std::wstring s) {
        if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
        return s;
    }

    CHARRANGE CurrentSel() {
        CHARRANGE cr;
        SendMessageW(edit_, EM_EXGETSEL, 0, (LPARAM)&cr);
        return cr;
    }

    std::wstring GetRange(LONG a, LONG b) {
        if (b <= a) return std::wstring();
        std::vector<wchar_t> buf(b - a + 2);
        TEXTRANGEW tr;
        tr.chrg.cpMin = a; tr.chrg.cpMax = b;
        tr.lpstrText = &buf[0];
        LONG n = (LONG)SendMessageW(edit_, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        return std::wstring(&buf[0], std::max(0L, n));
    }

    static bool ParseHyperlinkInstr(const std::wstring& t, std::wstring& url) {
        size_t p = t.rfind(L"HYPERLINK");
        if (p == std::wstring::npos) return false;
        size_t q1 = t.find(L'"', p);
        if (q1 == std::wstring::npos) return false;
        size_t q2 = t.find(L'"', q1 + 1);
        if (q2 == std::wstring::npos) return false;
        url = t.substr(q1 + 1, q2 - q1 - 1);
        return !url.empty();
    }

    std::wstring ResolveLinkUrl(const CHARRANGE& chrg) {
        std::wstring text = GetRange(chrg.cpMin, chrg.cpMax);
        std::wstring url;
        // RichEdit may expose the field instruction inside or just before the link range
        if (ParseHyperlinkInstr(text, url)) return url;
        std::wstring before = GetRange(std::max(0L, chrg.cpMin - 2100), chrg.cpMin);
        while (!before.empty() && before[before.size() - 1] == L' ') before.erase(before.size() - 1);
        if (!before.empty() && before[before.size() - 1] == L'"' && ParseHyperlinkInstr(before, url)) {
            // only accept an instruction that directly precedes the link text
            std::wstring instr = L"HYPERLINK \"" + url + L"\"";
            if (before.size() >= instr.size() && before.compare(before.size() - instr.size(), instr.size(), instr) == 0)
                return url;
        }
        // fall back to our own table: count earlier links with the same text
        std::vector<const md::LinkRef*> same;
        for (size_t i = 0; i < links_.size(); i++) if (links_[i].text == text) same.push_back(&links_[i]);
        if (same.size() == 1) return same[0]->url;
        if (same.size() > 1) {
            bool allSame = true;
            for (size_t i = 1; i < same.size(); i++) if (same[i]->url != same[0]->url) allSame = false;
            if (allSame) return same[0]->url;
            // occurrence index of this link text among earlier link-formatted occurrences
            int k = 0;
            LONG pos = 0;
            CHARRANGE saved = CurrentSel();
            SendMessageW(edit_, WM_SETREDRAW, FALSE, 0);
            for (;;) {
                FINDTEXTEXW ft;
                ft.chrg.cpMin = pos; ft.chrg.cpMax = chrg.cpMin;
                ft.lpstrText = text.c_str();
                LONG idx = (LONG)SendMessageW(edit_, EM_FINDTEXTEXW, FR_DOWN | FR_MATCHCASE, (LPARAM)&ft);
                if (idx < 0) break;
                CHARRANGE cr = { idx, idx + 1 };
                SendMessageW(edit_, EM_EXSETSEL, 0, (LPARAM)&cr);
                CHARFORMAT2W cf; cf.cbSize = sizeof(cf); cf.dwMask = CFM_LINK;
                SendMessageW(edit_, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
                if (cf.dwEffects & CFE_LINK) k++;
                pos = ft.chrgText.cpMax;
            }
            SendMessageW(edit_, EM_EXSETSEL, 0, (LPARAM)&saved);
            SendMessageW(edit_, WM_SETREDRAW, TRUE, 0);
            return same[std::min((size_t)k, same.size() - 1)]->url;
        }
        // plain URL text
        if (text.find(L"://") != std::wstring::npos || text.compare(0, 4, L"www.") == 0) return text;
        return std::wstring();
    }

    int Dpi() {
        HDC dc = GetDC(edit_);
        int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(edit_, dc);
        return dpi > 0 ? dpi : 96;
    }

    int PadX() { return MulDiv(14, Dpi(), 96); }
    int PadY() { return MulDiv(6, Dpi(), 96); }

    void UpdateFormatRect() {
        RECT r; GetClientRect(edit_, &r);
        r.left += PadX(); r.right -= PadX(); r.top += PadY();
        if (r.right <= r.left) r.right = r.left + 1;
        SendMessageW(edit_, EM_SETRECT, 0, (LPARAM)&r);
    }

    int TextWidthTwips() {
        RECT r; GetClientRect(edit_, &r);
        int px = r.right - r.left - 2 * PadX();
        if (!(GetWindowLongW(edit_, GWL_STYLE) & WS_VSCROLL)) px -= GetSystemMetrics(SM_CXVSCROLL);
        if (px < 200) px = 200;
        return MulDiv(px, 1440, Dpi());
    }

    LONG FirstVisibleChar() {
        int first = (int)SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0);
        return (LONG)SendMessageW(edit_, EM_LINEINDEX, first, 0);
    }

    void EnsureVisible(LONG cp) {
        int line = (int)SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, cp);
        int first = (int)SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0);
        RECT r; SendMessageW(edit_, EM_GETRECT, 0, (LPARAM)&r);
        POINTL pt = { r.left + 1, r.bottom - 2 };
        LONG lastChar = (LONG)SendMessageW(edit_, EM_CHARFROMPOS, 0, (LPARAM)&pt);
        int last = (int)SendMessageW(edit_, EM_EXLINEFROMCHAR, 0, lastChar);
        if (line < first || line >= last) {
            int target = std::max(0, line - 3);
            SendMessageW(edit_, EM_LINESCROLL, 0, target - first);
        }
        if (host_) host_->OnViewScrolled();
    }

    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        RichEditView* self = (RichEditView*)GetPropW(hwnd, kPropSelf);
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        WNDPROC old = self->oldProc_;
        auto CallOld = [old](HWND h, UINT m, WPARAM w, LPARAM l) { return CallWindowProcW(old, h, m, w, l); };
        switch (msg) {
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CHAR:
        case WM_SYSCHAR:
            if (self->host_ && self->host_->OnViewKey(msg, wp, lp)) return 0;
            break;
        case WM_VSCROLL:
        case WM_MOUSEWHEEL: {
            LRESULT r = CallOld(hwnd, msg, wp, lp);
            if (self->host_) self->host_->OnViewScrolled();
            return r;
        }
        case WM_SETFOCUS:
        case WM_LBUTTONUP: {
            LRESULT r = CallOld(hwnd, msg, wp, lp);
            HideCaret(hwnd);
            return r;
        }
        case WM_NCDESTROY:
            self->Unsubclass(hwnd);
            self->edit_ = NULL;
            return CallWindowProcW(old, hwnd, msg, wp, lp);
        }
        return CallOld(hwnd, msg, wp, lp);
    }
};

// ---------------------------------------------------------------- printing (adapted from the TC sample)

int RichEditView::Print(const std::wstring& title, const RECT* margins) {
    RECT defMargins = { 100, 100, 100, 100 };   // 10 mm
    if (!margins) margins = &defMargins;
    PRINTDLGW pd;
    memset(&pd, 0, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.Flags = PD_ALLPAGES | PD_USEDEVMODECOPIESANDCOLLATE | PD_RETURNDC | PD_NOSELECTION;
    CHARRANGE sel = CurrentSel();
    if (sel.cpMin != sel.cpMax) pd.Flags &= ~PD_NOSELECTION;
    pd.nFromPage = 1; pd.nToPage = 1; pd.nMinPage = 1; pd.nMaxPage = 0xFFFF;
    pd.nCopies = 1;
    pd.hwndOwner = GetAncestor(edit_, GA_ROOT);
    if (!PrintDlgW(&pd)) return LISTPLUGIN_ERROR;

    HDC hdc = pd.hDC;
    BOOL printSel = (pd.Flags & PD_SELECTION) != 0;
    BOOL printPages = (pd.Flags & PD_PAGENUMS) != 0;
    int pageFrom = 1, pageTo = 0x7FFF;
    if (printPages) { pageFrom = pd.nFromPage; pageTo = pd.nToPage > 0 ? pd.nToPage : 0x7FFF; }

    DOCINFOW di;
    memset(&di, 0, sizeof(di));
    di.cbSize = sizeof(di);
    di.lpszDocName = title.c_str();
    if (StartDocW(hdc, &di) > 0) {
        // page geometry in twips
        int dpiX = GetDeviceCaps(hdc, LOGPIXELSX), dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        int offX = GetDeviceCaps(hdc, PHYSICALOFFSETX), offY = GetDeviceCaps(hdc, PHYSICALOFFSETY);
        int physW = GetDeviceCaps(hdc, PHYSICALWIDTH), physH = GetDeviceCaps(hdc, PHYSICALHEIGHT);
        // margins: 0.1 mm -> device pixels
        int ml = MulDiv(margins->left, dpiX, 254), mr = MulDiv(margins->right, dpiX, 254);
        int mt = MulDiv(margins->top, dpiY, 254), mb = MulDiv(margins->bottom, dpiY, 254);
        RECT rc;
        rc.left = std::max(0, ml - offX);
        rc.top = std::max(0, mt - offY);
        rc.right = std::min(GetDeviceCaps(hdc, HORZRES), physW - mr - offX);
        rc.bottom = std::min(GetDeviceCaps(hdc, VERTRES), physH - mb - offY);
        FORMATRANGE fr;
        memset(&fr, 0, sizeof(fr));
        fr.hdc = fr.hdcTarget = hdc;
        fr.rc.left = MulDiv(rc.left, 1440, dpiX);
        fr.rc.top = MulDiv(rc.top, 1440, dpiY);
        fr.rc.right = MulDiv(rc.right, 1440, dpiX);
        fr.rc.bottom = MulDiv(rc.bottom, 1440, dpiY);
        fr.rcPage = fr.rc;
        RECT saved = fr.rc;

        LONG maxLen = (LONG)SendMessageW(edit_, WM_GETTEXTLENGTH, 0, 0);
        LONG cp = 0;
        if (printSel) { cp = sel.cpMin; maxLen = sel.cpMax; }
        SendMessageW(edit_, EM_FORMATRANGE, 0, 0);
        int page = 1;
        while (cp < maxLen && page <= pageTo) {
            fr.chrg.cpMin = cp;
            fr.chrg.cpMax = printSel ? maxLen : -1;
            fr.rc = saved; fr.rcPage = saved;
            bool render = page >= pageFrom;
            if (render) StartPage(hdc);
            LONG next = (LONG)SendMessageW(edit_, EM_FORMATRANGE, render ? 1 : 0, (LPARAM)&fr);
            if (render) EndPage(hdc);
            if (next <= cp) break;
            cp = next;
            page++;
        }
        SendMessageW(edit_, EM_FORMATRANGE, 0, 0);
        EndDoc(hdc);
    }
    DeleteDC(hdc);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    return LISTPLUGIN_OK;
}

} // namespace

IMarkdownView* CreateRichEditView() { return new RichEditView(); }
