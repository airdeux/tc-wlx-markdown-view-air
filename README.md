# TcWlxMarkdownViewAir – Markdown viewer for Total Commander

A lightweight Lister (WLX) plugin for [Total Commander](https://www.ghisler.com).

Shows Markdown files (`.md`, `.markdown`, `.mdown`, `.mkd`, `.mkdn`, `.mdwn`, `.mdtxt`, `.mdtext`, `.rmd`)
in Lister (F3) and Quick View (Ctrl+Q) as a formatted, "rich" document, with a raw source mode one key away.

No runtime dependencies: one DLL per architecture (`TcWlxMarkdownViewAir.wlx` / `TcWlxMarkdownViewAir.wlx64`),
using only Windows system DLLs (RichEdit `Msftedit.dll`, comdlg32, shell32).

## Keys

| Key | Action |
|---|---|
| **Ctrl+M**, **Ctrl+Tab**, **Ctrl+U** | Toggle between preview (formatted / rich) and source mode (raw Markdown, monospaced, light highlighting) |
| **F1**, **?** | This help page; **F1**, **?** or **Esc** returns to the file |
| **F7**, **Alt+F7**, **Ctrl+F** | Lister's Find dialog |
| **F3** / **Shift+F3** | Find next / previous |
| **F2** | Reload the file from disk (keeps mode and scroll position) |
| **Esc** | Close Lister |
| **Up / Down** | Scroll one line |
| **PgUp / PgDn** | Scroll one page |
| Home / End | Top / bottom |
| Left / Right | Horizontal scroll (source mode, wrapping off) |
| Ctrl+C, Ctrl+Ins / Ctrl+A | Copy selection / select all |

All other keys (N, P, S, H, 1–7, …) are passed on to Lister, so its own single-letter
commands keep working.
The last used mode is remembered.

When the file is changed on disk (e.g. saved in an editor), the view refreshes by itself
within about a second, keeping the mode and scroll position (turn off with `AutoReload=0`).

Links are clickable: web/mail links open in the default browser/mail client, `#anchor` links jump
to the heading, relative links open the referenced file (executables and scripts are blocked).

## Supported Markdown

CommonMark core plus GitHub extensions: ATX/setext headings, emphasis (`*`, `_`, `**`, `***`),
`~~strike~~`, `==mark==`, inline code, fenced/indented code blocks, nested block quotes,
nested bullet/ordered lists, task lists (`- [ ]`, `- [x]`), tables with alignment, horizontal rules,
inline/reference/auto links, bare URLs, HTML entities, common inline HTML (`<b>`, `<i>`, `<kbd>`,
`<br>`, `<a href>`, `<img alt>`, `<sup>`, …), HTML blocks (tags stripped), HTML comments (hidden),
YAML front matter (shown as a code block), footnote references.
Images are shown as `[▣ alt text]` placeholders. Dark mode follows Total Commander's dark mode.

## Installation

1. Download `TcWlxMarkdownViewAir.zip` from the [latest release](../../releases/latest).
2. Open the zip in Total Commander (Enter on the file) and confirm the installation prompt.
   The default install directory is `plugins\wlx\TcWlxMarkdownViewAir`.

Manual alternative: Configuration → Options → Plugins → Lister plugins (WLX) → Add, and select
`TcWlxMarkdownViewAir.wlx` (64-bit Total Commander picks up `TcWlxMarkdownViewAir.wlx64` automatically).

Requirements: Windows 7 or later, Total Commander 32- or 64-bit.

## Settings (optional)

Section `[MarkdownView]` in Lister's plugin ini (`lsplugin.ini`, next to `wincmd.ini`):

```ini
[MarkdownView]
StartMode=last          ; last | preview | source
FontName=Segoe UI
CodeFontName=Consolas
FontSize=11             ; points
CodeFontSize=10
Extensions=md,markdown,mdown,mkd,mkdn,mdwn,mdtxt,mdtext,rmd
MaxFileSizeMB=16        ; larger files are left to other viewers
PreviewMaxSizeMB=4      ; larger files open in source mode (Ctrl+M still renders them)
AutoReload=1            ; 1 = reload automatically when the file changes on disk, 0 = off
```

After changing `Extensions`, re-add the plugin so TC refreshes the detect string.

## Building

* Visual Studio 2017+ with CMake: run `build_msvc.bat` → `bin\TcWlxMarkdownViewAir.wlx`, `bin\TcWlxMarkdownViewAir.wlx64`
* MinGW-w64 (MSYS2 or Linux cross-compile): `make -f Makefile.mingw`
* Parser unit tests (any platform, no Windows needed): `make -f Makefile.mingw test`

### Smoke test on Windows

`lister_test_host.exe TcWlxMarkdownViewAir.wlx64 tests\sample.md` (built by CMake as
`lister_test_host`) loads the plugin into a fake Lister window and checks loading, mode toggling
(Ctrl+M / Ctrl+Tab / Ctrl+U), help (F1), F2 reload and auto-reload, navigation keys, key forwarding
(Esc, F3, F7, Alt+F7, S, P, H), search (first/next/previous/match case), percent notifications,
dark mode, resize and close.

## Architecture

```
listplug.cpp      TC exports (ListLoad[W], ListLoadNext[W], ListSearchText[W], ListSendCommand,
                  ListPrint[W], ListCloseWindow, ListGetDetectString, ListSetDefaultParams)
lister.cpp        controller: container window (= TC's ListWin), keys, modes, settings, links
md_view.h         IMarkdownView back-end interface  <-- rendering back-ends plug in here
richedit_view.cpp RichEdit back-end (current)
md_parser.*       Markdown -> document tree (no Windows dependency)
md_rtf.*          document tree -> RTF (RichEdit back-end)
md_html.*         document tree -> HTML (ready for a future WebView2 back-end; used by tests)
text_load.*       file reading with BOM / UTF-8 / ANSI detection
MarkdownView.rc   version info + this README.md embedded as the help page
```

Switching to WebView2 later means adding one `IMarkdownView` implementation that displays
`md::RenderHtml()` output and routes keys/link clicks to `IViewHost` — the parser, controller,
key handling and TC interface stay unchanged (see the comment at the top of `md_view.h`).
No WebView2 SDK or loader is referenced yet.

## License

[MIT](LICENSE) © 2026 Gábor Erdei

`src/listplug.h` follows the Lister plugin interface from the
[Total Commander WLX SDK](https://github.com/ghisler/WLX-SDK) by Christian Ghisler; the plugin was
started from the SDK's `listplugsample`. Total Commander is a trademark of Christian Ghisler.
