// md_html.cpp - document tree -> HTML (for a future WebView2 back-end / tests)
#include "md_html.h"
#include <cwctype>

namespace md {
namespace {

typedef std::wstring WS;

WS Esc(const WS& s, bool attr = false) {
    WS r;
    for (size_t i = 0; i < s.size(); i++) {
        wchar_t c = s[i];
        if (c == '&') r += L"&amp;";
        else if (c == '<') r += L"&lt;";
        else if (c == '>') r += L"&gt;";
        else if (c == '"' && attr) r += L"&quot;";
        else r += c;
    }
    return r;
}

bool SafeUrl(const WS& u) {
    WS l;
    for (size_t i = 0; i < u.size() && i < 16; i++) l += (wchar_t)towlower(u[i]);
    return l.compare(0, 11, L"javascript:") != 0 && l.compare(0, 9, L"vbscript:") != 0 && l.compare(0, 5, L"data:") != 0;
}

const wchar_t* kCss =
    L"body{font-family:'Segoe UI',system-ui,sans-serif;font-size:11pt;line-height:1.5;color:#1f2328;"
    L"max-width:980px;margin:0 auto;padding:16px 32px;}"
    L"h1,h2{border-bottom:1px solid #d0d7de;padding-bottom:.3em}"
    L"h1,h2,h3,h4,h5,h6{margin:24px 0 16px;font-weight:600;line-height:1.25}"
    L"h6{color:#59636e}a{color:#0969da;text-decoration:none}a:hover{text-decoration:underline}"
    L"code,kbd,pre{font-family:Consolas,monospace;font-size:10pt}"
    L"code{background:#eaeef2;padding:.1em .4em;border-radius:4px}"
    L"kbd{border:1px solid #d0d7de;border-bottom-width:2px;border-radius:4px;padding:0 .3em;background:#f6f8fa}"
    L"pre{background:#f6f8fa;padding:12px 16px;border-radius:6px;overflow:auto}pre code{background:none;padding:0}"
    L"blockquote{margin:0 0 16px;padding:0 1em;color:#59636e;border-left:.25em solid #d0d7de}"
    L"table{border-collapse:collapse;margin-bottom:16px}th,td{border:1px solid #d0d7de;padding:6px 13px}"
    L"tr:nth-child(2n){background:#f6f8fa}hr{border:0;height:.25em;background:#d0d7de;margin:24px 0}"
    L"li.task{list-style:none}li.task input{margin:0 .3em 0 -1.4em}img{max-width:100%}mark{background:#fff8c5}"
    L"::selection{background:#b6d7ff}";

class HtmlRenderer {
public:
    WS o;

    void Inl(const Inlines& inl) {
        for (size_t i = 0; i < inl.size(); i++) {
            const Span& s = inl[i];
            if (s.lineBreak) { o += L"<br>\n"; continue; }
            WS open, close;
            unsigned f = s.flags;
            if (f & F_LINK) {
                bool linkStart = i == 0 || !(inl[i - 1].flags & F_LINK) || inl[i - 1].url != s.url || inl[i - 1].lineBreak;
                bool linkEnd = i + 1 == inl.size() || !(inl[i + 1].flags & F_LINK) || inl[i + 1].url != s.url || inl[i + 1].lineBreak;
                if (linkStart) open += L"<a href=\"" + (SafeUrl(s.url) ? Esc(s.url, true) : WS(L"#")) + L"\">";
                if (linkEnd) close = L"</a>" + close;
            }
            if (f & F_BOLD) { open += L"<strong>"; close = L"</strong>" + close; }
            if (f & F_ITALIC) { open += L"<em>"; close = L"</em>" + close; }
            if (f & F_STRIKE) { open += L"<del>"; close = L"</del>" + close; }
            if (f & F_UNDERLINE) { open += L"<u>"; close = L"</u>" + close; }
            if (f & F_SUP) { open += L"<sup>"; close = L"</sup>" + close; }
            if (f & F_SUB) { open += L"<sub>"; close = L"</sub>" + close; }
            if (f & F_MARK) { open += L"<mark>"; close = L"</mark>" + close; }
            if (f & F_KBD) { open += L"<kbd>"; close = L"</kbd>" + close; }
            else if (f & F_CODE) { open += L"<code>"; close = L"</code>" + close; }
            // the link close tag must be outermost
            if ((f & F_LINK) && close.size() >= 4 && close.compare(close.size() - 4, 4, L"</a>") != 0) {
                size_t a = close.find(L"</a>");
                if (a != WS::npos) { close.erase(a, 4); close += L"</a>"; }
            }
            o += open;
            if (f & F_IMAGE) {
                // images are not loaded (no network / local files); show alt text placeholder
                o += L"<span class=\"img\" title=\"" + Esc(s.url, true) + L"\">[" + Esc(s.text) + L"]</span>";
            } else {
                o += Esc(s.text);
            }
            o += close;
        }
    }

    void Blocks(const std::vector<Block>& bs, bool tight) {
        for (size_t i = 0; i < bs.size(); i++) BlockOut(bs[i], tight);
    }

    void BlockOut(const Block& b, bool tight) {
        switch (b.type) {
        case BlockType::Paragraph:
        case BlockType::Html:
            if (b.inl.empty()) break;
            if (!tight) o += L"<p>";
            Inl(b.inl);
            o += tight ? L"\n" : L"</p>\n";
            break;
        case BlockType::Heading: {
            WS lv = std::to_wstring(b.level);
            o += L"<h" + lv + L" id=\"" + Esc(b.anchor, true) + L"\">";
            Inl(b.inl);
            o += L"</h" + lv + L">\n";
            break;
        }
        case BlockType::CodeBlock:
        case BlockType::FrontMatter: {
            o += L"<pre><code";
            if (!b.info.empty()) {
                WS lang = b.info.substr(0, b.info.find(' '));
                o += L" class=\"language-" + Esc(lang, true) + L"\"";
            }
            o += L">" + Esc(b.text) + L"\n</code></pre>\n";
            break;
        }
        case BlockType::Quote:
            o += L"<blockquote>\n";
            Blocks(b.children, false);
            o += L"</blockquote>\n";
            break;
        case BlockType::List:
            if (b.ordered) o += b.start != 1 ? L"<ol start=\"" + std::to_wstring(b.start) + L"\">\n" : WS(L"<ol>\n");
            else o += L"<ul>\n";
            for (size_t i = 0; i < b.children.size(); i++) {
                const Block& it = b.children[i];
                if (it.task >= 0) {
                    o += L"<li class=\"task\"><input type=\"checkbox\" disabled";
                    if (it.task) o += L" checked";
                    o += L">";
                } else o += L"<li>";
                Blocks(it.children, b.tight);
                o += L"</li>\n";
            }
            o += b.ordered ? L"</ol>\n" : L"</ul>\n";
            break;
        case BlockType::Rule:
            o += L"<hr>\n";
            break;
        case BlockType::Table: {
            o += L"<table>\n";
            for (size_t r = 0; r < b.rows.size(); r++) {
                if (r == 0) o += L"<thead>\n";
                if (r == 1) o += L"<tbody>\n";
                o += L"<tr>";
                for (size_t c = 0; c < b.rows[r].size(); c++) {
                    WS tag = r == 0 ? L"th" : L"td";
                    o += L"<" + tag;
                    Align a = c < b.aligns.size() ? b.aligns[c] : Align::Default;
                    if (a == Align::Left) o += L" style=\"text-align:left\"";
                    else if (a == Align::Center) o += L" style=\"text-align:center\"";
                    else if (a == Align::Right) o += L" style=\"text-align:right\"";
                    o += L">";
                    Inl(b.rows[r][c]);
                    o += L"</" + tag + L">";
                }
                o += L"</tr>\n";
                if (r == 0) o += L"</thead>\n";
            }
            if (b.rows.size() > 1) o += L"</tbody>\n";
            o += L"</table>\n";
            break;
        }
        default:
            Blocks(b.children, false);
            break;
        }
    }
};

} // namespace

std::wstring RenderHtml(const Block& doc, const std::wstring& title, bool fullDocument) {
    HtmlRenderer r;
    if (fullDocument) {
        r.o += L"<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>" + Esc(title) + L"</title>\n<style>";
        r.o += kCss;
        r.o += L"</style></head><body>\n";
    }
    r.Blocks(doc.children, false);
    if (fullDocument) r.o += L"</body></html>\n";
    return r.o;
}

} // namespace md
