// md_view.h - rendering back-end interface
//
// The lister controller (lister.cpp) owns the TC-facing container window, keyboard
// routing, modes and settings. The actual display is delegated to an IMarkdownView
// back-end. Today there is one back-end, RichEditView (richedit_view.cpp).
//
// Adding a WebView2 back-end later:
//   1. implement IMarkdownView in e.g. webview2_view.cpp, rendering the same
//      md::Block tree with md::RenderHtml() (md_html.cpp) for preview mode;
//   2. route keyboard input to IViewHost::OnViewKey() (WebView2:
//      ICoreWebView2Controller::add_AcceleratorKeyPressed) and link clicks to
//      IViewHost::OnLinkClicked() (add_NavigationStarting, cancel + forward);
//   3. return it from CreateView() for Renderer=webview2 (see lister.cpp).
// Nothing else in the plugin needs to change.

#pragma once
#include <windows.h>
#include <string>
#include "md_parser.h"

enum class ViewMode { Preview, Source };

enum class ScrollCmd { LineUp, LineDown, PageUp, PageDown, Top, Bottom, Left, Right };

enum class Backend { RichEdit /*, WebView2 */ };

struct ViewSettings {
    std::wstring bodyFont = L"Segoe UI";
    std::wstring codeFont = L"Consolas";
    int  fontSize = 11;          // points
    int  codeFontSize = 10;      // points
    bool wrapSource = true;      // wrap long lines in source mode (TC "wrap text" option)
    bool dark = false;           // TC dark mode
};

// Everything a back-end needs to display a file
struct DocumentData {
    std::wstring path;           // full file name
    std::wstring baseDir;        // directory incl. trailing backslash (relative links/images)
    std::wstring source;         // file text, UTF-16
    md::Block doc;               // parsed document
};

// Implemented by the controller, called by back-ends
class IViewHost {
public:
    virtual ~IViewHost() {}
    // Raw keyboard message from the view. Return true if consumed.
    virtual bool OnViewKey(UINT msg, WPARAM wp, LPARAM lp) = 0;
    // The visible position changed (scroll bar, wheel ...) -> update Lister's percent display
    virtual void OnViewScrolled() = 0;
    // A link was activated
    virtual void OnLinkClicked(const std::wstring& url) = 0;
};

class IMarkdownView {
public:
    virtual ~IMarkdownView() {}
    virtual bool Create(HWND parent, IViewHost* host) = 0;
    virtual HWND Window() const = 0;
    virtual void Show(const DocumentData& data, ViewMode mode, const ViewSettings& s) = 0;
    virtual void Resize(int cx, int cy) = 0;
    virtual void Focus() = 0;
    virtual void Scroll(ScrollCmd cmd) = 0;
    virtual int  GetPercent() = 0;
    virtual void SetPercent(int percent) = 0;
    // Opaque scroll position used to keep the place across a reload of the same file
    // (RichEdit: first visible line).
    virtual int  GetScrollPosition() = 0;
    virtual void SetScrollPosition(int pos) = 0;
    // flags = Lister lcs_* flags. Returns true if found (and selected/scrolled into view).
    virtual bool Find(const std::wstring& text, int flags) = 0;
    virtual void Copy() = 0;
    virtual void SelectAll() = 0;
    // margins as passed by TC to ListPrint (0.1 mm units, MM_LOMETRIC)
    virtual int  Print(const std::wstring& title, const RECT* margins) = 0;
    virtual bool ScrollToAnchor(const std::wstring& anchor) = 0;
    // WM_NOTIFY / WM_COMMAND received by the container from the back-end's child windows
    virtual bool OnParentNotify(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) = 0;
};

IMarkdownView* CreateRichEditView();
