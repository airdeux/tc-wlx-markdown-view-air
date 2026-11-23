// md_rtf.cpp - document tree -> RTF for RichEdit (Msftedit, RichEdit 4.1+)
//
// RichEdit limitations worked around here:
//  * paragraph borders/shading are not displayed -> code blocks use character
//    highlight padded to a rectangle, block quotes use a bar glyph + indent,
//    horizontal rules use a row of box-drawing characters.
//  * tables are real RTF tables (\trowd ... \row), supported by Msftedit.

#include "md_rtf.h"
#include <cstdio>
#include <algorithm>

namespace md {
namespace {

// colour table indices
enum {
    C_TEXT = 1, C_LINK = 2, C_CODEBG = 3, C_GRAY = 4, C_BORDER = 5, C_BLOCKBG = 6,
    C_HEAD = 7, C_MARK = 8, C_QUOTEBAR = 9, C_SRCHEAD = 10, C_SRCMARK = 11
};

const char* kColorTable =
    "{\\colortbl;"
    "\\red31\\green35\\blue40;"      // 1 text
    "\\red9\\green105\\blue218;"     // 2 link
    "\\red234\\green238\\blue242;"   // 3 inline code background
    "\\red101\\green109\\blue118;"   // 4 gray (quotes, image placeholders)
    "\\red208\\green215\\blue222;"   // 5 borders / rules
    "\\red246\\green248\\blue250;"   // 6 code block / table header background
    "\\red20\\green24\\blue30;"      // 7 headings
    "\\red255\\green236\\blue153;"   // 8 ==mark==
    "\\red200\\green207\\blue214;"   // 9 quote bar
    "\\red5\\green80\\blue174;"      // 10 source: headings
    "\\red130\\green80\\blue223;"    // 11 source: markers / emphasis
    "}\n";

const char* kColorTableDark =
    "{\\colortbl;"
    "\\red230\\green237\\blue243;"   // 1 text
    "\\red88\\green166\\blue255;"    // 2 link
    "\\red52\\green58\\blue66;"      // 3 inline code background
    "\\red145\\green152\\blue161;"   // 4 gray
    "\\red61\\green68\\blue77;"      // 5 borders / rules
    "\\red22\\green27\\blue34;"      // 6 code block / table header background
    "\\red240\\green246\\blue252;"   // 7 headings
    "\\red109\\green84\\blue0;"      // 8 ==mark==
    "\\red72\\green79\\blue88;"      // 9 quote bar
    "\\red121\\green192\\blue255;"   // 10 source: headings
    "\\red210\\green168\\blue255;"   // 11 source: markers
    "}\n";

typedef std::wstring WS;

class Writer {
public:
    explicit Writer(const RtfStyle& st) : st_(st) {}

    void Esc(const WS& t) {
        for (size_t i = 0; i < t.size(); i++) {
            unsigned long c = (unsigned long)t[i];
            if (c > 0xFFFF) {  // 32-bit wchar_t (non-Windows builds): split into surrogates
                c -= 0x10000;
                EscUnit(0xD800 + (c >> 10));
                EscUnit(0xDC00 + (c & 0x3FF));
                continue;
            }
            EscUnit(c);
        }
    }

    void EscUnit(unsigned long c) {
        if (c == '\\' || c == '{' || c == '}') { o += '\\'; o += (char)c; }
        else if (c == '\t') o += "\\tab ";
        else if (c == '\n') o += "\\line ";
        else if (c < 0x20) { /* drop control chars */ }
        else if (c == 0xA0) o += "\\~";
        else if (c < 0x80) o += (char)c;
        else {
            char b[24];
            snprintf(b, sizeof(b), "\\u%d?", (int)(short)(unsigned short)c);
            o += b;
        }
    }

    void Raw(const char* s) { o += s; }
    void Rawf(const char* fmt, int a) { char b[128]; snprintf(b, sizeof(b), fmt, a); o += b; }
    void Rawf(const char* fmt, int a, int b2) { char b[128]; snprintf(b, sizeof(b), fmt, a, b2); o += b; }

    void Header() {
        o += "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\\deflang1033\n{\\fonttbl";
        o += "{\\f0\\fswiss\\fcharset0 "; Esc(st_.bodyFont); o += ";}";
        o += "{\\f1\\fmodern\\fcharset0 "; Esc(st_.codeFont); o += ";}";
        o += "{\\f2\\fnil\\fcharset0 "; Esc(st_.symbolFont); o += ";}";
        o += "}\n";
        o += st_.dark ? kColorTableDark : kColorTable;
        o += "\\viewkind4\\uc1\n";
    }

    int Base() const { return st_.baseSize * 2; }   // half-points
    int Code() const { return st_.codeSize * 2; }

    std::string o;
    const RtfStyle& st_;
};

// ------------------------------------------------------------------ rich renderer

struct Ctx {
    int indent = 0;          // left indent (twips)
    int quote = 0;           // block quote depth
    int listDepth = 0;
    bool tight = false;      // inside a tight list
};

class RichRenderer {
public:
    RichRenderer(const RtfStyle& st, RtfOutput& out) : w_(st), st_(st), out_(out) {}

    void Render(const Block& doc) {
        w_.Header();
        Ctx ctx;
        RenderChildren(doc.children, ctx);
        // final empty paragraph keeps the last block's formatting from leaking
        w_.Raw("\\pard\\plain\\fs4\\par\n}");
        out_.rtf.swap(w_.o);
    }

private:
    Writer w_;
    const RtfStyle& st_;
    RtfOutput& out_;
    // pending prefix for the next paragraph (list bullet / quote bar)
    struct Prefix { bool active = false; WS text; bool symbolFont = false; int color = C_TEXT; int hang = 0; };
    Prefix prefix_;
    WS* capture_ = nullptr;     // collects the displayed text of headings (for anchor lookup)
    int curSize_ = 22;          // font size (half-points) of the current paragraph

    int SpacingAfter(const Ctx& ctx) const { return ctx.tight ? 40 : 160; }

    void ParaStart(const Ctx& ctx, int sb, int sa, int sizeHalf, int color, bool bold, const char* align = "\\ql") {
        w_.Raw("\\pard\\plain");
        w_.Raw(align);
        int li = ctx.indent;
        int fi = 0;
        if (prefix_.active) { fi = -prefix_.hang; }
        w_.Rawf("\\li%d\\fi%d", li, fi);
        if (prefix_.active) w_.Rawf("\\tx%d", li);
        w_.Rawf("\\sb%d\\sa%d", sb, sa);
        w_.Raw("\\sl276\\slmult1");
        w_.Rawf("\\f0\\fs%d\\cf%d", sizeHalf, color);
        curSize_ = sizeHalf;
        if (bold) w_.Raw("\\b");
        w_.Raw(" ");
        if (prefix_.active) {
            w_.Raw("{");
            if (prefix_.symbolFont) w_.Raw("\\f2");
            w_.Rawf("\\cf%d\\b0\\i0 ", prefix_.color);
            w_.Esc(prefix_.text);
            w_.Raw("}\\tab ");
            prefix_.active = false;
        }
    }

    void ParaEnd() { w_.Raw("\\par\n"); }

    void Runs(const Inlines& inl) {
        size_t i = 0;
        while (i < inl.size()) {
            const Span& s = inl[i];
            if ((s.flags & F_LINK) && !s.lineBreak) {
                // group consecutive spans with the same target into one hyperlink field
                size_t j = i;
                WS text;
                while (j < inl.size() && !inl[j].lineBreak && (inl[j].flags & F_LINK) && inl[j].url == s.url) {
                    text += inl[j].text; j++;
                }
                LinkRef lr; lr.text = text; lr.url = s.url;
                out_.links.push_back(lr);
                w_.Raw("{\\field{\\*\\fldinst{HYPERLINK \"");
                WS u = s.url;
                // quotes and backslashes are not allowed inside the field instruction string
                for (size_t k = 0; k < u.size(); k++) if (u[k] == '"') u[k] = '\'';
                WS u2;
                for (size_t k = 0; k < u.size(); k++) { if (u[k] == '\\') u2 += L"\\\\"; else u2 += u[k]; }
                w_.Esc(u2);
                w_.Raw("\"}}{\\fldrslt{");
                for (size_t k = i; k < j; k++) Run(inl[k], true);
                w_.Raw("}}}");
                i = j;
                continue;
            }
            Run(s, false);
            i++;
        }
    }

    void Run(const Span& s, bool inLink) {
        if (s.lineBreak) { w_.Raw("\\line "); if (capture_) *capture_ += L'\v'; return; }
        w_.Raw("{");
        unsigned f = s.flags;
        if (f & F_BOLD) w_.Raw("\\b");
        if (f & F_ITALIC) w_.Raw("\\i");
        if (f & F_STRIKE) w_.Raw("\\strike");
        if ((f & F_UNDERLINE) || inLink) w_.Raw("\\ul");
        if (f & F_SUP) w_.Raw("\\super");
        if (f & F_SUB) w_.Raw("\\sub");
        if (f & F_MARK) w_.Rawf("\\highlight%d", C_MARK);
        if (inLink) w_.Rawf("\\cf%d", C_LINK);
        bool code = (f & (F_CODE | F_KBD)) != 0;
        if (code) {
            w_.Rawf("\\f1\\fs%d", std::max(8, curSize_ * w_.Code() / w_.Base()));
            w_.Rawf("\\highlight%d", C_CODEBG);
        }
        if (f & F_IMAGE) {
            if (!inLink) w_.Rawf("\\cf%d", C_GRAY);
            w_.Raw("\\i ");
            w_.Esc(L"[\u25A3 ");       // ▣ image placeholder
            w_.Esc(s.text);
            w_.Esc(L"]");
            w_.Raw("}");
            if (capture_) *capture_ += L"[\u25A3 " + s.text + L"]";
            return;
        }
        if (w_.o[w_.o.size() - 1] != '{') w_.Raw(" ");   // terminate the last control word
        if (code) {
            w_.Esc(L"\u00A0");
            w_.Esc(s.text);
            w_.Esc(L"\u00A0");
            if (capture_) *capture_ += L"\u00A0" + s.text + L"\u00A0";
        } else {
            w_.Esc(s.text);
            if (capture_) *capture_ += s.text;
        }
        w_.Raw("}");
    }

    void RenderChildren(const std::vector<Block>& ch, const Ctx& ctx) {
        for (size_t i = 0; i < ch.size(); i++) RenderBlock(ch[i], ctx);
    }

    void ApplyQuoteBar(const Ctx& ctx) {
        if (ctx.quote > 0 && !prefix_.active) {
            prefix_.active = true;
            prefix_.text = L"\u2503";      // ┃
            prefix_.symbolFont = false;
            prefix_.color = C_QUOTEBAR;
            prefix_.hang = 240;
        }
    }

    void RenderBlock(const Block& b, const Ctx& ctx) {
        switch (b.type) {
        case BlockType::Paragraph:
        case BlockType::Html: {
            if (b.inl.empty()) { prefix_.active = false; return; }
            ApplyQuoteBar(ctx);
            ParaStart(ctx, 0, SpacingAfter(ctx), w_.Base(), ctx.quote ? C_GRAY : C_TEXT, false);
            Runs(b.inl);
            ParaEnd();
            break;
        }
        case BlockType::Heading: {
            static const int scale[7] = { 0, 200, 155, 128, 112, 100, 92 };   // percent of base size
            int lv = std::max(1, std::min(6, b.level));
            int size = w_.Base() * scale[lv] / 100;
            int sb = lv <= 2 ? 300 : 220;
            HeadingRef h; h.anchor = b.anchor; h.level = lv;
            ApplyQuoteBar(ctx);
            ParaStart(ctx, sb, lv <= 2 ? 80 : 100, size, lv == 6 ? C_GRAY : C_HEAD, true);
            capture_ = &h.text;
            Runs(b.inl);
            capture_ = nullptr;
            out_.headings.push_back(h);
            ParaEnd();
            if (lv <= 2) Rule(ctx, C_BORDER, 120);
            break;
        }
        case BlockType::CodeBlock:
        case BlockType::FrontMatter:
            CodeBlock(b, ctx);
            break;
        case BlockType::Quote: {
            Ctx c2 = ctx;
            c2.quote++;
            c2.indent += 240;
            prefix_.active = false;
            RenderChildren(b.children, c2);
            break;
        }
        case BlockType::List:
            List(b, ctx);
            break;
        case BlockType::Rule:
            prefix_.active = false;
            Rule(ctx, C_BORDER, 160);
            break;
        case BlockType::Table:
            prefix_.active = false;
            Table(b, ctx);
            break;
        default:
            RenderChildren(b.children, ctx);
            break;
        }
    }

    void Rule(const Ctx& ctx, int color, int sa) {
        const int fs = 16;                           // 8pt
        int charTw = fs * 10 * 55 / 100;             // box drawing char in a monospaced font: 0.55em
        int width = st_.pageWidthTwips - ctx.indent - 60;
        int n = std::max(10, width / std::max(1, charTw));
        w_.Raw("\\pard\\plain\\ql");
        w_.Rawf("\\li%d\\fi0\\sb0\\sa%d\\sl0", ctx.indent, sa);
        w_.Rawf("\\f1\\fs%d\\cf%d ", fs, color);
        w_.Esc(WS(n, L'\u2500'));
        ParaEnd();
    }

    void CodeBlock(const Block& b, const Ctx& ctx) {
        prefix_.active = false;
        std::vector<WS> lines;
        size_t st = 0;
        for (;;) {
            size_t e = b.text.find('\n', st);
            if (e == WS::npos) { lines.push_back(b.text.substr(st)); break; }
            lines.push_back(b.text.substr(st, e - st));
            st = e + 1;
        }
        size_t maxLen = 0;
        for (size_t i = 0; i < lines.size(); i++) maxLen = std::max(maxLen, lines[i].size());
        int code = w_.Code();
        int charTw = code * 10 * 55 / 100;
        int avail = (st_.pageWidthTwips - ctx.indent - 400) / std::max(1, charTw);  // chars that fit without wrapping
        size_t width = maxLen + 2;
        size_t minWidth = (size_t)std::max(20, std::min(avail, 100));
        if (width < minWidth) width = minWidth;
        int textColor = b.type == BlockType::FrontMatter ? C_GRAY : C_TEXT;

        // thin padding line above/below (spaces in a small font)
        const int padFs = 8;
        size_t padChars = width * code / padFs;
        auto padLine = [&](int sb, int sa) {
            w_.Raw("\\pard\\plain\\ql");
            w_.Rawf("\\li%d\\fi0\\sb%d", ctx.indent, sb);
            w_.Rawf("\\sa%d\\sl0", sa);
            w_.Rawf("\\f1\\fs%d\\highlight%d ", padFs, C_BLOCKBG);
            w_.Esc(WS(padChars, L'\u00A0'));
            ParaEnd();
        };
        padLine(40, 0);
        for (size_t i = 0; i < lines.size(); i++) {
            w_.Raw("\\pard\\plain\\ql");
            w_.Rawf("\\li%d\\fi0\\sb0\\sa0\\sl0", ctx.indent);
            w_.Rawf("\\f1\\fs%d\\cf%d", code, textColor);
            w_.Rawf("\\highlight%d ", C_BLOCKBG);
            WS l = L"\u00A0" + lines[i];
            if (l.size() < width) l.append(width - l.size(), L'\u00A0');
            else l += L"\u00A0";
            // keep normal spaces inside the line so long lines can still wrap
            w_.Esc(l);
            ParaEnd();
        }
        padLine(0, ctx.tight ? 80 : 180);
    }

    void List(const Block& b, const Ctx& ctx) {
        static const wchar_t* bullets[] = { L"\u2022", L"\u25E6", L"\u25AA" };   // • ◦ ▪
        int maxNum = b.start + (int)b.children.size() - 1;
        int hang = b.ordered ? (maxNum >= 100 ? 520 : maxNum >= 10 ? 420 : 340) : 300;
        Ctx c2 = ctx;
        c2.indent = ctx.indent + hang + (ctx.listDepth == 0 ? 120 : 0);
        c2.listDepth = ctx.listDepth + 1;
        c2.tight = b.tight;
        for (size_t k = 0; k < b.children.size(); k++) {
            const Block& item = b.children[k];
            prefix_.active = true;
            prefix_.hang = hang;
            prefix_.color = ctx.quote ? C_GRAY : C_TEXT;
            prefix_.symbolFont = false;
            if (item.task >= 0) {
                prefix_.text = item.task ? L"\u2611" : L"\u2610";   // ☑ ☐
                prefix_.symbolFont = true;
            } else if (b.ordered) {
                prefix_.text = std::to_wstring(b.start + (int)k) + L".";
            } else {
                prefix_.text = bullets[ctx.listDepth % 3];
            }
            bool firstIsText = !item.children.empty() &&
                (item.children[0].type == BlockType::Paragraph || item.children[0].type == BlockType::Heading ||
                 item.children[0].type == BlockType::Html) && !item.children[0].inl.empty();
            if (!firstIsText) {
                // marker on its own line (empty item, or item starting with code/list/...)
                ParaStart(c2, 0, item.children.empty() ? SpacingAfter(c2) : 0, w_.Base(), C_TEXT, false);
                ParaEnd();
            }
            RenderChildren(item.children, c2);
            prefix_.active = false;
        }
        if (b.tight && ctx.listDepth == 0) {
            // small gap after a top-level tight list
            w_.Raw("\\pard\\plain\\fs8\\sa60\\par\n");
        }
    }

    void Table(const Block& b, const Ctx& ctx) {
        if (b.rows.empty()) return;
        size_t cols = b.aligns.size();
        if (cols == 0) return;
        // natural column widths from plain text length
        std::vector<int> chars(cols, 3);
        for (size_t r = 0; r < b.rows.size(); r++)
            for (size_t c = 0; c < cols && c < b.rows[r].size(); c++)
                chars[c] = std::max(chars[c], std::min(60, (int)PlainText(b.rows[r][c]).size()));
        int charTw = w_.Base() * 10 * 52 / 100;   // average proportional char width ~0.52em
        const int gap = 108;
        std::vector<int> width(cols);
        int total = 0;
        for (size_t c = 0; c < cols; c++) { width[c] = chars[c] * charTw + 2 * gap + 120; total += width[c]; }
        int avail = std::max(2000, st_.pageWidthTwips - ctx.indent - 200);
        if (total > avail) {
            int t2 = 0;
            for (size_t c = 0; c < cols; c++) { width[c] = std::max(600, width[c] * avail / total); t2 += width[c]; }
            total = t2;
        }
        const char* border = "\\brdrs\\brdrw10\\brdrcf5";
        curSize_ = w_.Base();
        for (size_t r = 0; r < b.rows.size(); r++) {
            bool hdr = r == 0;
            w_.Rawf("\\trowd\\trgaph%d\\trleft%d", gap, ctx.indent);
            w_.Raw("\\trpaddl108\\trpaddr108\\trpaddfl3\\trpaddfr3\\trpaddt40\\trpaddb40\\trpaddft3\\trpaddfb3");
            int x = ctx.indent;
            for (size_t c = 0; c < cols; c++) {
                w_.Raw("\\clbrdrt"); w_.Raw(border);
                w_.Raw("\\clbrdrl"); w_.Raw(border);
                w_.Raw("\\clbrdrb"); w_.Raw(border);
                w_.Raw("\\clbrdrr"); w_.Raw(border);
                if (hdr || (r % 2) == 0) w_.Rawf("\\clcbpat%d", C_BLOCKBG);
                x += width[c];
                w_.Rawf("\\cellx%d", x);
            }
            w_.Raw("\n");
            for (size_t c = 0; c < cols; c++) {
                const char* al = "\\ql";
                if (b.aligns[c] == Align::Center) al = "\\qc";
                else if (b.aligns[c] == Align::Right) al = "\\qr";
                w_.Raw("\\pard\\plain\\intbl");
                w_.Raw(al);
                w_.Rawf("\\sb30\\sa30\\f0\\fs%d\\cf%d", w_.Base(), C_TEXT);
                if (hdr) w_.Raw("\\b");
                w_.Raw(" ");
                if (c < b.rows[r].size()) Runs(b.rows[r][c]);
                w_.Raw("\\cell ");
            }
            w_.Raw("\\row\n");
        }
        w_.Raw("\\pard\\plain\\fs8\\sa120\\par\n");
    }
};

} // namespace

unsigned long RtfBackground(bool dark) {
    return dark ? 0x0017110DUL : 0x00FFFFFFUL;   // #0d1117 / white
}

RtfOutput RenderRtf(const Block& doc, const RtfStyle& style) {
    RtfOutput out;
    RichRenderer r(style, out);
    r.Render(doc);
    return out;
}

std::string RenderSourceRtf(const std::wstring& source, const RtfStyle& style) {
    Writer w(style);
    w.Header();
    w.Rawf("\\pard\\plain\\ql\\li0\\fi0\\sb0\\sa0\\sl0\\f1\\fs%d\\cf%d ", w.Code(), C_TEXT);
    bool inFence = false;
    wchar_t fenceCh = 0;
    size_t fenceLen = 0;
    size_t st = 0, n = source.size();
    bool firstLine = true;
    while (st <= n) {
        size_t e = st;
        while (e < n && source[e] != '\n' && source[e] != '\r') e++;
        WS line = source.substr(st, e - st);
        if (!firstLine) w.Raw("\\par\n");
        firstLine = false;

        // light highlighting
        size_t ind = 0;
        while (ind < line.size() && line[ind] == ' ') ind++;
        int color = 0; bool bold = false;
        if (style.sourceHighlight) {
            bool fenceLine = false;
            if (ind < 4 && ind < line.size() && (line[ind] == '`' || line[ind] == '~')) {
                size_t k = ind; while (k < line.size() && line[k] == line[ind]) k++;
                if (k - ind >= 3) {
                    fenceLine = true;
                    if (!inFence) { inFence = true; fenceCh = line[ind]; fenceLen = k - ind; }
                    else if (line[ind] == fenceCh && k - ind >= fenceLen) inFence = false;
                }
            }
            if (fenceLine || inFence) color = C_GRAY;
            else if (ind < 4 && ind < line.size() && line[ind] == '#') { color = C_SRCHEAD; bold = true; }
            else if (ind < 4 && ind < line.size() && line[ind] == '>') color = C_GRAY;
        }
        if (color || bold) {
            w.Raw("{");
            if (color) w.Rawf("\\cf%d", color);
            if (bold) w.Raw("\\b");
            w.Raw(" ");
            w.Esc(line);
            w.Raw("}");
        } else {
            w.Esc(line);
        }

        if (e >= n) break;
        if (source[e] == '\r' && e + 1 < n && source[e + 1] == '\n') e++;
        st = e + 1;
    }
    w.Raw("\\par\n}");
    return w.o;
}

} // namespace md
