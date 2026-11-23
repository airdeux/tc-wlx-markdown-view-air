// md_parser.cpp - Markdown -> document tree
//
// Supported: ATX/setext headings, paragraphs, hard/soft breaks, emphasis (* _ ** __ ***),
// ~~strikethrough~~, `code spans`, fenced and indented code blocks, block quotes (nested),
// bullet/ordered/task lists (nested), thematic breaks, GFM tables with alignment,
// inline links, reference links, autolinks (<...> and bare URLs), images (shown as
// placeholders), HTML entities, common inline HTML tags, HTML blocks (tags stripped),
// HTML comments (hidden), YAML front matter, footnote references (superscript).

#include "md_parser.h"
#include <algorithm>

namespace md {
namespace {

typedef std::wstring WS;
typedef std::vector<WS> Lines;

struct LinkRef { WS url; WS title; };
typedef std::map<WS, LinkRef> RefMap;

// ---------------------------------------------------------------- character helpers

bool IsWs(wchar_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || c == 0xA0; }
bool IsAsciiPunct(wchar_t c) {
    return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126);
}
bool IsPunct(wchar_t c) {
    return IsAsciiPunct(c) || (c >= 0x2000 && c <= 0x206F) || (c >= 0x3000 && c <= 0x303F) ||
           (c >= 0xFF01 && c <= 0xFF0F) || c == 0xA1 || c == 0xA7 || c == 0xAB || c == 0xBB || c == 0xBF;
}
bool IsDigit(wchar_t c) { return c >= '0' && c <= '9'; }
bool IsAlpha(wchar_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsAlnum(wchar_t c) { return c && !IsWs(c) && !IsPunct(c); }

wchar_t LowerCh(wchar_t c) {
    if (c >= 'A' && c <= 'Z') return (wchar_t)(c - 'A' + 'a');
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return (wchar_t)(c + 0x20);
    if (c >= 0x391 && c <= 0x3A9) return (wchar_t)(c + 0x20);          // Greek
    if (c >= 0x410 && c <= 0x42F) return (wchar_t)(c + 0x20);          // Cyrillic
    if (c >= 0x100 && c <= 0x17F && (c % 2) == 0) return (wchar_t)(c + 1); // Latin Ext-A (most pairs, incl. Hungarian ő/ű)
    return c;
}
WS Lower(WS s) { for (size_t i = 0; i < s.size(); i++) s[i] = LowerCh(s[i]); return s; }

WS TrimRight(const WS& s) { size_t e = s.size(); while (e > 0 && IsWs(s[e - 1])) e--; return s.substr(0, e); }
WS TrimLeft(const WS& s) { size_t b = 0; while (b < s.size() && IsWs(s[b])) b++; return s.substr(b); }
WS Trim(const WS& s) { return TrimLeft(TrimRight(s)); }

bool StartsWithCI(const WS& s, size_t p, const wchar_t* pre) {
    for (size_t i = 0; pre[i]; i++) {
        if (p + i >= s.size() || LowerCh(s[p + i]) != pre[i]) return false;
    }
    return true;
}

void AppendCodepoint(WS& out, unsigned long cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
        cp -= 0x10000;
        out += (wchar_t)(0xD800 + (cp >> 10));
        out += (wchar_t)(0xDC00 + (cp & 0x3FF));
    } else {
        out += (wchar_t)cp;
    }
}

WS NormLabel(const WS& s) {
    WS r; bool sp = false;
    for (size_t i = 0; i < s.size(); i++) {
        if (IsWs(s[i])) { sp = true; continue; }
        if (sp && !r.empty()) r += ' ';
        sp = false;
        r += LowerCh(s[i]);
    }
    return r;
}

// Process backslash escapes and entities in URLs / titles
bool DecodeEntity(const WS& s, size_t p, WS& out, size_t& end);
WS Unescape(const WS& s) {
    WS r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && IsAsciiPunct(s[i + 1])) { r += s[++i]; continue; }
        if (s[i] == '&') { size_t e; if (DecodeEntity(s, i, r, e)) { i = e - 1; continue; } }
        r += s[i];
    }
    return r;
}

// ---------------------------------------------------------------- entities

struct NamedEntity { const wchar_t* name; unsigned cp; };
const NamedEntity kEntities[] = {
    {L"amp", '&'}, {L"lt", '<'}, {L"gt", '>'}, {L"quot", '"'}, {L"apos", '\''}, {L"nbsp", 0xA0},
    {L"copy", 0xA9}, {L"reg", 0xAE}, {L"trade", 0x2122}, {L"hellip", 0x2026}, {L"mdash", 0x2014},
    {L"ndash", 0x2013}, {L"lsquo", 0x2018}, {L"rsquo", 0x2019}, {L"ldquo", 0x201C}, {L"rdquo", 0x201D},
    {L"laquo", 0xAB}, {L"raquo", 0xBB}, {L"bull", 0x2022}, {L"middot", 0xB7}, {L"deg", 0xB0},
    {L"plusmn", 0xB1}, {L"times", 0xD7}, {L"divide", 0xF7}, {L"larr", 0x2190}, {L"rarr", 0x2192},
    {L"uarr", 0x2191}, {L"darr", 0x2193}, {L"harr", 0x2194}, {L"lArr", 0x21D0}, {L"rArr", 0x21D2},
    {L"euro", 0x20AC}, {L"pound", 0xA3}, {L"yen", 0xA5}, {L"cent", 0xA2}, {L"sect", 0xA7},
    {L"para", 0xB6}, {L"ensp", 0x2002}, {L"emsp", 0x2003}, {L"thinsp", 0x2009}, {L"zwj", 0x200D},
    {L"zwnj", 0x200C}, {L"shy", 0xAD}, {L"iexcl", 0xA1}, {L"iquest", 0xBF}, {L"micro", 0xB5},
    {L"frac12", 0xBD}, {L"frac14", 0xBC}, {L"frac34", 0xBE}, {L"sup2", 0xB2}, {L"sup3", 0xB3},
    {L"le", 0x2264}, {L"ge", 0x2265}, {L"ne", 0x2260}, {L"infin", 0x221E}, {L"check", 0x2713},
    {L"hearts", 0x2665}, {L"star", 0x2606}, {L"num", '#'}, {L"ast", '*'}, {L"lowbar", '_'},
    {L"verbar", '|'}, {L"grave", '`'}, {L"tilde", 0x2DC}, {L"Auml", 0xC4}, {L"auml", 0xE4},
    {L"Ouml", 0xD6}, {L"ouml", 0xF6}, {L"Uuml", 0xDC}, {L"uuml", 0xFC}, {L"szlig", 0xDF},
    {L"eacute", 0xE9}, {L"Eacute", 0xC9}, {L"aacute", 0xE1}, {L"Aacute", 0xC1}, {L"iacute", 0xED},
    {L"oacute", 0xF3}, {L"uacute", 0xFA}, {L"egrave", 0xE8}, {L"agrave", 0xE0}, {L"ccedil", 0xE7},
    {L"ntilde", 0xF1}, {L"alpha", 0x3B1}, {L"beta", 0x3B2}, {L"gamma", 0x3B3}, {L"delta", 0x3B4},
    {L"pi", 0x3C0}, {L"mu", 0x3BC}, {L"sigma", 0x3C3}, {L"lambda", 0x3BB}, {L"omega", 0x3C9},
};

bool DecodeEntity(const WS& s, size_t p, WS& out, size_t& end) {
    // s[p] == '&'
    size_t semi = s.find(';', p + 1);
    if (semi == WS::npos || semi - p > 32 || semi == p + 1) return false;
    WS name = s.substr(p + 1, semi - p - 1);
    if (name[0] == '#') {
        unsigned long cp = 0;
        bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        size_t i = hex ? 2 : 1;
        if (i >= name.size()) return false;
        for (; i < name.size(); i++) {
            wchar_t c = name[i];
            int v;
            if (IsDigit(c)) v = c - '0';
            else if (hex && c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else return false;
            cp = cp * (hex ? 16 : 10) + v;
            if (cp > 0x10FFFF) return false;
        }
        AppendCodepoint(out, cp);
        end = semi + 1;
        return true;
    }
    for (size_t i = 0; i < sizeof(kEntities) / sizeof(kEntities[0]); i++) {
        if (name == kEntities[i].name) {
            AppendCodepoint(out, kEntities[i].cp);
            end = semi + 1;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- line helpers

WS ExpandTabs(const WS& l) {
    if (l.find('\t') == WS::npos) return l;
    WS r; size_t col = 0;
    for (size_t i = 0; i < l.size(); i++) {
        if (l[i] == '\t') { size_t n = 4 - (col % 4); r.append(n, ' '); col += n; }
        else { r += l[i]; col++; }
    }
    return r;
}

int Indent(const WS& l) { int n = 0; while (n < (int)l.size() && l[n] == ' ') n++; return n; }
bool IsBlank(const WS& l) { for (size_t i = 0; i < l.size(); i++) if (!IsWs(l[i])) return false; return true; }

bool IsHr(const WS& l) {
    int ind = Indent(l);
    if (ind >= 4 || ind >= (int)l.size()) return false;
    wchar_t c = l[ind];
    if (c != '-' && c != '*' && c != '_') return false;
    int cnt = 0;
    for (size_t i = ind; i < l.size(); i++) {
        if (l[i] == c) cnt++;
        else if (l[i] != ' ' && l[i] != '\t') return false;
    }
    return cnt >= 3;
}

bool AtxHeading(const WS& l, int& level, WS& content) {
    int ind = Indent(l);
    if (ind >= 4) return false;
    size_t p = ind; int n = 0;
    while (p < l.size() && l[p] == '#') { p++; n++; }
    if (n < 1 || n > 6) return false;
    if (p < l.size() && l[p] != ' ') return false;
    WS c = Trim(l.substr(p));
    // closing sequence
    size_t e = c.size();
    while (e > 0 && c[e - 1] == '#') e--;
    if (e == 0) c.clear();
    else if (e < c.size() && c[e - 1] == ' ') c = TrimRight(c.substr(0, e));
    level = n; content = c;
    return true;
}

bool FenceStart(const WS& l, wchar_t& ch, int& len, WS& info, int& ind) {
    ind = Indent(l);
    if (ind >= 4 || ind >= (int)l.size()) return false;
    ch = l[ind];
    if (ch != '`' && ch != '~') return false;
    size_t p = ind;
    while (p < l.size() && l[p] == ch) p++;
    len = (int)(p - ind);
    if (len < 3) return false;
    info = Trim(l.substr(p));
    if (ch == '`' && info.find('`') != WS::npos) return false;
    return true;
}

bool IsFenceClose(const WS& l, wchar_t ch, int len) {
    if (Indent(l) >= 4) return false;
    WS t = Trim(l);
    if ((int)t.size() < len) return false;
    for (size_t i = 0; i < t.size(); i++) if (t[i] != ch) return false;
    return true;
}

bool FenceOpenAtEnd(const Lines& ls) {
    bool open = false; wchar_t ch = 0; int len = 0;
    for (size_t i = 0; i < ls.size(); i++) {
        if (!open) {
            WS info; int ind;
            if (FenceStart(ls[i], ch, len, info, ind)) open = true;
        } else if (IsFenceClose(ls[i], ch, len)) open = false;
    }
    return open;
}

struct ListMarkerInfo {
    bool ordered = false;
    wchar_t ch = 0;           // bullet char or delimiter ('.' or ')')
    int number = 1;
    int contentIndent = 0;    // column where item content starts
    bool empty = false;
};

bool ListMarker(const WS& l, ListMarkerInfo& m) {
    int ind = Indent(l);
    if (ind >= 4 || ind >= (int)l.size()) return false;
    size_t p = ind;
    wchar_t c = l[p];
    if (c == '-' || c == '+' || c == '*') {
        m.ordered = false; m.ch = c; p++;
    } else if (IsDigit(c)) {
        size_t q = p; long num = 0;
        while (q < l.size() && IsDigit(l[q]) && q - p < 9) { num = num * 10 + (l[q] - '0'); q++; }
        if (q >= l.size() || (l[q] != '.' && l[q] != ')')) return false;
        m.ordered = true; m.ch = l[q]; m.number = (int)num; p = q + 1;
    } else return false;
    size_t markerEnd = p;
    if (p < l.size() && l[p] != ' ') return false;
    size_t s = 0;
    while (p + s < l.size() && l[p + s] == ' ') s++;
    if (p + s >= l.size()) { m.empty = true; m.contentIndent = (int)markerEnd + 1; }
    else if (s >= 5) m.contentIndent = (int)markerEnd + 1;
    else m.contentIndent = (int)(markerEnd + s);
    return true;
}

bool IsQuoteStart(const WS& l) { int ind = Indent(l); return ind < 4 && ind < (int)l.size() && l[ind] == '>'; }

const wchar_t* kBlockTags[] = {
    L"address", L"article", L"aside", L"blockquote", L"body", L"center", L"details", L"dialog", L"dd",
    L"div", L"dl", L"dt", L"fieldset", L"figcaption", L"figure", L"footer", L"form", L"h1", L"h2",
    L"h3", L"h4", L"h5", L"h6", L"header", L"hr", L"html", L"li", L"main", L"nav", L"ol", L"p",
    L"picture", L"pre", L"section", L"summary", L"table", L"tbody", L"td", L"tfoot", L"th", L"thead",
    L"tr", L"ul", L"img", L"a", L"br", L"source", L"video", L"script", L"style", L"span", L"b", L"i",
    L"strong", L"em", L"sup", L"sub", L"kbd", 0
};

WS TagNameAt(const WS& l, size_t p) {  // l[p] == '<'
    size_t q = p + 1;
    if (q < l.size() && l[q] == '/') q++;
    size_t st = q;
    while (q < l.size() && (IsAlpha(l[q]) || (q > st && IsDigit(l[q])))) q++;
    if (q == st) return WS();
    if (q < l.size() && !(l[q] == ' ' || l[q] == '>' || l[q] == '/' || l[q] == '\t')) return WS();
    if (q >= l.size()) return WS();
    return Lower(l.substr(st, q - st));
}

bool IsHtmlBlockStart(const WS& l, bool interrupting) {
    int ind = Indent(l);
    if (ind >= 4 || ind >= (int)l.size() || l[ind] != '<') return false;
    if (l.compare(ind, 4, L"<!--") == 0) return true;
    WS name = TagNameAt(l, ind);
    if (name.empty()) return false;
    for (int i = 0; kBlockTags[i]; i++) {
        if (name == kBlockTags[i]) {
            // inline-ish tags only start a block at the beginning of a block, never interrupt a paragraph
            if (interrupting && (name == L"a" || name == L"img" || name == L"br" || name == L"span" ||
                                 name == L"b" || name == L"i" || name == L"strong" || name == L"em" ||
                                 name == L"sup" || name == L"sub" || name == L"kbd"))
                return false;
            return true;
        }
    }
    return false;
}

// Table helpers
std::vector<WS> SplitRow(const WS& line) {
    WS t = Trim(line);
    if (!t.empty() && t[0] == '|') t = t.substr(1);
    if (!t.empty() && t[t.size() - 1] == '|' && !(t.size() >= 2 && t[t.size() - 2] == '\\')) t = t.substr(0, t.size() - 1);
    std::vector<WS> cells; WS cur;
    for (size_t i = 0; i < t.size(); i++) {
        wchar_t c = t[i];
        if (c == '\\' && i + 1 < t.size() && t[i + 1] == '|') { cur += '|'; i++; continue; }
        if (c == '\\' && i + 1 < t.size()) { cur += c; cur += t[++i]; continue; }
        if (c == '`') {  // keep code spans intact (pipes inside code still split in GFM, but be lenient)
            size_t q = i; while (q < t.size() && t[q] == '`') q++;
            size_t run = q - i;
            size_t r = q; bool found = false;
            while (r < t.size()) {
                if (t[r] == '`') { size_t r2 = r; while (r2 < t.size() && t[r2] == '`') r2++; if (r2 - r == run) { found = true; r = r2; break; } r = r2; }
                else r++;
            }
            if (found) { cur += t.substr(i, r - i); i = r - 1; continue; }
        }
        if (c == '|') { cells.push_back(Trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    cells.push_back(Trim(cur));
    return cells;
}

bool IsDelimRow(const WS& line, std::vector<Align>& aligns) {
    if (Indent(line) >= 4) return false;
    WS t = Trim(line);
    if (t.empty()) return false;
    bool hasPipe = t.find('|') != WS::npos;
    std::vector<WS> cells = SplitRow(t);
    aligns.clear();
    for (size_t i = 0; i < cells.size(); i++) {
        const WS& c = cells[i];
        if (c.empty()) return false;
        bool l = c[0] == ':', r = c[c.size() - 1] == ':';
        size_t b = l ? 1 : 0, e = c.size() - (r ? 1 : 0);
        if (e <= b) return false;
        for (size_t k = b; k < e; k++) if (c[k] != '-') return false;
        aligns.push_back(l && r ? Align::Center : r ? Align::Right : l ? Align::Left : Align::Default);
    }
    if (!hasPipe && cells.size() < 2) return false;
    return !aligns.empty();
}

bool TableStartsAt(const Lines& L, size_t i) {
    if (i + 1 >= L.size()) return false;
    if (L[i].find('|') == WS::npos || Indent(L[i]) >= 4) return false;
    std::vector<Align> al;
    if (!IsDelimRow(L[i + 1], al)) return false;
    return SplitRow(L[i]).size() == al.size();
}

bool RefDef(const WS& line, WS& label, WS& url, WS& title) {
    if (Indent(line) >= 4) return false;
    WS t = Trim(line);
    if (t.size() < 4 || t[0] != '[') return false;
    size_t e = t.find(L"]:");
    if (e == WS::npos || e < 2) return false;
    label = t.substr(1, e - 1);
    if (label[0] == '^' || label.find('[') != WS::npos || label.find(']') != WS::npos) return false;
    WS rest = Trim(t.substr(e + 2));
    if (rest.empty()) return false;
    size_t p = 0;
    if (rest[0] == '<') {
        size_t g = rest.find('>');
        if (g == WS::npos) return false;
        url = rest.substr(1, g - 1); p = g + 1;
    } else {
        while (p < rest.size() && !IsWs(rest[p])) p++;
        url = rest.substr(0, p);
    }
    WS tail = Trim(rest.substr(p));
    title.clear();
    if (!tail.empty()) {
        wchar_t o = tail[0], c = o == '(' ? ')' : o;
        if ((o != '"' && o != '\'' && o != '(') || tail.size() < 2 || tail[tail.size() - 1] != c) return false;
        title = tail.substr(1, tail.size() - 2);
    }
    url = Unescape(url);
    return true;
}

// ---------------------------------------------------------------- inline parser

class InlineParser {
public:
    explicit InlineParser(const RefMap& refs) : refs_(refs) {}

    Inlines Parse(const WS& s) {
        Inlines out;
        out_ = &out;
        htmlFlags_ = 0; htmlUrl_.clear();
        ParseRange(s, 0, WS());
        // trim trailing whitespace / breaks
        while (!out.empty() && out.back().lineBreak) out.pop_back();
        if (!out.empty()) {
            Span& last = out.back();
            if (!(last.flags & F_CODE)) { last.text = TrimRight(last.text); if (last.text.empty() && !(last.flags & F_IMAGE)) out.pop_back(); }
        }
        while (!out.empty() && out.front().lineBreak) out.erase(out.begin());
        if (!out.empty() && !(out.front().flags & F_CODE)) {
            out.front().text = TrimLeft(out.front().text);
            if (out.front().text.empty() && !(out.front().flags & F_IMAGE) && !out.front().lineBreak) out.erase(out.begin());
        }
        return out;
    }

private:
    const RefMap& refs_;
    Inlines* out_ = nullptr;
    unsigned htmlFlags_ = 0;
    WS htmlUrl_;
    int depth_ = 0;
    enum { kMaxInlineDepth = 32 };

    struct LinkResult { WS text, url, title; size_t end = 0; };

    void EmitText(const WS& t, unsigned flags, const WS& url) {
        if (t.empty()) return;
        if (!out_->empty()) {
            Span& l = out_->back();
            if (!l.lineBreak && l.flags == flags && l.url == url && !(flags & F_IMAGE)) { l.text += t; return; }
        }
        Span s; s.text = t; s.flags = flags; s.url = url;
        out_->push_back(s);
    }
    void EmitBreak() {
        Span s; s.lineBreak = true;
        out_->push_back(s);
    }
    void EmitBlockBreak() {  // break caused by block-level html tags; avoid duplicates
        if (out_->empty() || out_->back().lineBreak) return;
        // strip trailing spaces before the break
        Span& l = out_->back();
        if (!(l.flags & F_CODE)) l.text = TrimRight(l.text);
        if (l.text.empty() && !(l.flags & F_IMAGE)) { out_->pop_back(); if (out_->empty() || out_->back().lineBreak) return; }
        EmitBreak();
    }

    unsigned Eff(unsigned flags) const {
        unsigned f = flags | htmlFlags_;
        if (!htmlUrl_.empty()) f |= F_LINK;
        return f;
    }
    WS EffUrl(unsigned flags, const WS& url) const {
        if (flags & F_LINK) return url;
        return htmlUrl_;
    }

    static size_t SkipCodeSpan(const WS& s, size_t p) {  // s[p]=='`'; returns index after span or p+run if unclosed
        size_t n = s.size(), q = p;
        while (q < n && s[q] == '`') q++;
        size_t run = q - p, r = q;
        while (r < n) {
            if (s[r] == '`') {
                size_t r2 = r; while (r2 < n && s[r2] == '`') r2++;
                if (r2 - r == run) return r2;
                r = r2;
            } else r++;
        }
        return q;
    }

    bool FindCloser(const WS& s, size_t from, wchar_t d, size_t& cs, size_t& cl) {
        size_t n = s.size(), r = from;
        std::vector<size_t> stack;
        while (r < n) {
            wchar_t ch = s[r];
            if (ch == '\\') { r += 2; continue; }
            if (ch == '`') { r = SkipCodeSpan(s, r); continue; }
            if (ch == d) {
                size_t r2 = r; while (r2 < n && s[r2] == d) r2++;
                size_t len = r2 - r;
                wchar_t b = r > 0 ? s[r - 1] : ' ', a = r2 < n ? s[r2] : ' ';
                bool lf = !IsWs(a) && (!IsPunct(a) || IsWs(b) || IsPunct(b));
                bool rf = !IsWs(b) && (!IsPunct(b) || IsWs(a) || IsPunct(a));
                bool canClose = rf && (d == '*' || !lf || IsPunct(a));
                bool canOpen = lf && (d == '*' || !rf || IsPunct(b));
                if (canClose) {
                    size_t remaining = len;
                    while (remaining > 0 && !stack.empty()) {
                        if (stack.back() <= remaining) { remaining -= stack.back(); stack.pop_back(); }
                        else { stack.back() -= remaining; remaining = 0; }
                    }
                    if (remaining > 0 && r + (len - remaining) > from) {
                        cs = r + (len - remaining); cl = remaining;
                        return true;
                    }
                    if (remaining > 0 && canOpen) stack.push_back(remaining);
                } else if (canOpen) {
                    stack.push_back(len);
                }
                r = r2;
                continue;
            }
            r++;
        }
        return false;
    }

    bool TryLink(const WS& s, size_t p, LinkResult& lr) {  // s[p] == '['
        size_t n = s.size(), r = p;
        int depth = 0;
        for (; r < n; r++) {
            wchar_t ch = s[r];
            if (ch == '\\') { r++; continue; }
            if (ch == '`') { size_t e = SkipCodeSpan(s, r); r = e - 1; continue; }
            if (ch == '[') depth++;
            else if (ch == ']') { if (--depth == 0) break; }
        }
        if (r >= n) return false;
        WS text = s.substr(p + 1, r - p - 1);
        size_t after = r + 1;
        if (after < n && s[after] == '(') {
            size_t k = after + 1;
            while (k < n && IsWs(s[k])) k++;
            WS dest;
            if (k < n && s[k] == '<') {
                size_t e = s.find('>', k + 1);
                if (e == WS::npos) return false;
                dest = s.substr(k + 1, e - k - 1);
                if (dest.find('\n') != WS::npos) return false;
                k = e + 1;
            } else {
                int par = 0; size_t st = k;
                while (k < n) {
                    wchar_t ch = s[k];
                    if (ch == '\\' && k + 1 < n) { k += 2; continue; }
                    if (IsWs(ch)) break;
                    if (ch == '(') par++;
                    else if (ch == ')') { if (par == 0) break; par--; }
                    k++;
                }
                dest = s.substr(st, k - st);
            }
            while (k < n && IsWs(s[k])) k++;
            if (k < n && (s[k] == '"' || s[k] == '\'' || s[k] == '(')) {
                wchar_t close = s[k] == '(' ? ')' : s[k];
                size_t e = k + 1;
                while (e < n && s[e] != close) { if (s[e] == '\\') e++; e++; }
                if (e >= n) return false;
                lr.title = Unescape(s.substr(k + 1, e - k - 1));
                k = e + 1;
                while (k < n && IsWs(s[k])) k++;
            }
            if (k >= n || s[k] != ')') return false;
            lr.url = Unescape(dest); lr.text = text; lr.end = k + 1;
            return true;
        }
        WS label; size_t end;
        if (after + 1 < n && s[after] == '[') {
            size_t e = s.find(']', after + 1);
            if (e == WS::npos) return false;
            label = s.substr(after + 1, e - after - 1);
            if (label.empty()) label = text;
            end = e + 1;
        } else {
            label = text; end = after;
        }
        RefMap::const_iterator it = refs_.find(NormLabel(label));
        if (it == refs_.end()) return false;
        lr.url = it->second.url; lr.title = it->second.title; lr.text = text; lr.end = end;
        return true;
    }

    static WS GetAttr(const WS& attrs, const wchar_t* name) {
        WS low = Lower(attrs);
        WS key = name;
        size_t p = 0;
        while ((p = low.find(key, p)) != WS::npos) {
            bool boundary = p == 0 || IsWs(low[p - 1]);
            size_t q = p + key.size();
            while (q < low.size() && IsWs(low[q])) q++;
            if (boundary && q < low.size() && low[q] == '=') {
                q++;
                while (q < low.size() && IsWs(low[q])) q++;
                if (q >= attrs.size()) return WS();
                wchar_t qc = attrs[q];
                if (qc == '"' || qc == '\'') {
                    size_t e = attrs.find(qc, q + 1);
                    if (e == WS::npos) return WS();
                    return Unescape(attrs.substr(q + 1, e - q - 1));
                }
                size_t e = q;
                while (e < attrs.size() && !IsWs(attrs[e]) && attrs[e] != '>') e++;
                return Unescape(attrs.substr(q, e - q));
            }
            p = q;
        }
        return WS();
    }

    // Parses an HTML tag at s[p]=='<'. Returns false if it is not a tag.
    bool ParseTag(const WS& s, size_t p, WS& name, bool& closing, WS& attrs, size_t& end) {
        size_t n = s.size(), q = p + 1;
        closing = false;
        if (q < n && s[q] == '/') { closing = true; q++; }
        size_t st = q;
        while (q < n && (IsAlpha(s[q]) || (q > st && (IsDigit(s[q]) || s[q] == '-')))) q++;
        if (q == st) return false;
        name = Lower(s.substr(st, q - st));
        size_t as = q;
        wchar_t quote = 0;
        while (q < n) {
            wchar_t c = s[q];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '"' || c == '\'') quote = c;
            else if (c == '>') break;
            else if (c == '<') return false;
            q++;
        }
        if (q >= n) return false;
        if (q > as && !IsWs(s[as]) && s[as] != '/') return false;
        attrs = s.substr(as, q - as);
        end = q + 1;
        return true;
    }

    unsigned TagFlag(const WS& name) {
        if (name == L"b" || name == L"strong") return F_BOLD;
        if (name == L"i" || name == L"em" || name == L"cite" || name == L"var") return F_ITALIC;
        if (name == L"s" || name == L"del" || name == L"strike") return F_STRIKE;
        if (name == L"u" || name == L"ins") return F_UNDERLINE;
        if (name == L"code" || name == L"tt" || name == L"samp") return F_CODE;
        if (name == L"kbd") return F_KBD;
        if (name == L"sup") return F_SUP;
        if (name == L"sub") return F_SUB;
        if (name == L"mark") return F_MARK;
        if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') return F_BOLD;
        if (name == L"summary" || name == L"th" || name == L"dt") return F_BOLD;
        return 0;
    }

    static bool IsBreakTag(const WS& name) {
        static const wchar_t* tags[] = { L"p", L"div", L"li", L"tr", L"ul", L"ol", L"table", L"blockquote",
            L"center", L"details", L"summary", L"hr", L"h1", L"h2", L"h3", L"h4", L"h5", L"h6", L"dd", L"dt",
            L"dl", L"section", L"article", L"header", L"footer", L"figure", L"figcaption", L"pre", L"picture", 0 };
        for (int i = 0; tags[i]; i++) if (name == tags[i]) return true;
        return false;
    }

    void ParseRange(const WS& s, unsigned flags, const WS& url) {
        if (depth_ >= kMaxInlineDepth) {   // pathological nesting: plain text
            EmitText(s, Eff(flags), EffUrl(flags, url));
            return;
        }
        struct DepthGuard { int& d; DepthGuard(int& x) : d(x) { d++; } ~DepthGuard() { d--; } } guard(depth_);
        size_t n = s.size(), p = 0;
        WS buf;
        unsigned curFlags = Eff(flags);
        WS curUrl = EffUrl(flags, url);
        auto flush = [&]() {
            if (!buf.empty()) { EmitText(buf, curFlags, curUrl); buf.clear(); }
        };
        auto refresh = [&]() { curFlags = Eff(flags); curUrl = EffUrl(flags, url); };

        while (p < n) {
            wchar_t c = s[p];
            switch (c) {
            case '\\':
                if (p + 1 < n && IsAsciiPunct(s[p + 1])) { buf += s[p + 1]; p += 2; }
                else if (p + 1 < n && s[p + 1] == '\n') {
                    flush(); EmitBreak(); p += 2;
                    while (p < n && s[p] == ' ') p++;
                } else { buf += c; p++; }
                continue;
            case '`': {
                size_t q = p; while (q < n && s[q] == '`') q++;
                size_t run = q - p;
                size_t e = SkipCodeSpan(s, p);
                if (e == q) { buf.append(run, '`'); p = q; continue; }
                WS content = s.substr(q, e - run - q);
                for (size_t i = 0; i < content.size(); i++) if (content[i] == '\n') content[i] = ' ';
                if (content.size() >= 2 && content[0] == ' ' && content[content.size() - 1] == ' ' && !IsBlank(content))
                    content = content.substr(1, content.size() - 2);
                flush();
                EmitText(content, curFlags | F_CODE, curUrl);
                p = e;
                continue;
            }
            case '!':
                if (p + 1 < n && s[p + 1] == '[') {
                    LinkResult lr;
                    if (TryLink(s, p + 1, lr)) {
                        flush();
                        // alt text: plain text of the inner inlines
                        InlineParser sub(refs_);
                        Inlines altInl = sub.Parse(lr.text);
                        Span img; img.flags = curFlags | F_IMAGE; img.text = PlainText(altInl); img.url = lr.url;
                        if (curFlags & F_LINK) { img.url = curUrl; img.flags |= F_LINK; }
                        if (img.text.empty()) {
                            WS f = lr.url; size_t sl = f.find_last_of(L"/\\");
                            if (sl != WS::npos) f = f.substr(sl + 1);
                            size_t qm = f.find_first_of(L"?#");
                            if (qm != WS::npos) f = f.substr(0, qm);
                            img.text = f.empty() ? WS(L"image") : f;
                        }
                        out_->push_back(img);
                        p = lr.end;
                        continue;
                    }
                }
                buf += c; p++;
                continue;
            case '[': {
                LinkResult lr;
                if (TryLink(s, p, lr)) {
                    flush();
                    if (curFlags & F_LINK) ParseRange(lr.text, flags, url);
                    else ParseRange(lr.text, flags | F_LINK, lr.url);
                    refresh();
                    p = lr.end;
                    continue;
                }
                // footnote reference [^1]
                if (p + 2 < n && s[p + 1] == '^') {
                    size_t e = s.find(']', p + 2);
                    if (e != WS::npos && e > p + 2 && s.substr(p + 2, e - p - 2).find(' ') == WS::npos &&
                        !(e + 1 < n && s[e + 1] == ':')) {
                        flush();
                        EmitText(L"[" + s.substr(p + 2, e - p - 2) + L"]", curFlags | F_SUP, curUrl);
                        p = e + 1;
                        continue;
                    }
                }
                buf += c; p++;
                continue;
            }
            case '<': {
                // autolink
                size_t q = p + 1;
                if (q < n && IsAlpha(s[q])) {
                    size_t k = q;
                    while (k < n && (IsAlpha(s[k]) || IsDigit(s[k]) || s[k] == '+' || s[k] == '.' || s[k] == '-')) k++;
                    if (k < n && s[k] == ':' && k - q >= 2 && k - q <= 32) {
                        size_t e = k + 1;
                        while (e < n && s[e] != '>' && s[e] != '<' && !IsWs(s[e])) e++;
                        if (e < n && s[e] == '>') {
                            WS u = s.substr(q, e - q);
                            flush();
                            EmitText(u, curFlags | F_LINK, (curFlags & F_LINK) ? curUrl : u);
                            p = e + 1;
                            continue;
                        }
                    }
                    // email autolink
                    size_t e = q; bool at = false;
                    while (e < n && s[e] != '>' && !IsWs(s[e]) && s[e] != '<') { if (s[e] == '@') at = true; e++; }
                    if (at && e < n && s[e] == '>') {
                        WS u = s.substr(q, e - q);
                        flush();
                        EmitText(u, curFlags | F_LINK, (curFlags & F_LINK) ? curUrl : L"mailto:" + u);
                        p = e + 1;
                        continue;
                    }
                }
                // HTML comment
                if (s.compare(p, 4, L"<!--") == 0) {
                    size_t e = s.find(L"-->", p + 4);
                    if (e != WS::npos) { p = e + 3; continue; }
                }
                WS name, attrs; bool closing; size_t end;
                if (ParseTag(s, p, name, closing, attrs, end)) {
                    flush();
                    if (name == L"br") EmitBreak();
                    else if (name == L"img") {
                        Span img; img.flags = curFlags | F_IMAGE;
                        img.text = GetAttr(attrs, L"alt");
                        img.url = GetAttr(attrs, L"src");
                        if (curFlags & F_LINK) { img.url = curUrl; img.flags |= F_LINK; }
                        if (img.text.empty()) {
                            WS f = GetAttr(attrs, L"src"); size_t sl = f.find_last_of(L"/\\");
                            if (sl != WS::npos) f = f.substr(sl + 1);
                            img.text = f.empty() ? WS(L"image") : f;
                        }
                        out_->push_back(img);
                    } else if (name == L"a") {
                        if (closing) htmlUrl_.clear();
                        else if (!(flags & F_LINK)) {
                            WS href = GetAttr(attrs, L"href");
                            htmlUrl_ = href;
                        }
                    } else {
                        unsigned tf = TagFlag(name);
                        if (tf) { if (closing) htmlFlags_ &= ~tf; else htmlFlags_ |= tf; }
                        if (IsBreakTag(name)) EmitBlockBreak();
                    }
                    refresh();
                    p = end;
                    continue;
                }
                buf += c; p++;
                continue;
            }
            case '*':
            case '_': {
                size_t q = p; while (q < n && s[q] == c) q++;
                size_t run = q - p;
                wchar_t before = p > 0 ? s[p - 1] : ' ', after = q < n ? s[q] : ' ';
                bool lf = !IsWs(after) && (!IsPunct(after) || IsWs(before) || IsPunct(before));
                bool rf = !IsWs(before) && (!IsPunct(before) || IsWs(after) || IsPunct(after));
                bool canOpen = lf && (c == '*' || !rf || IsPunct(before));
                size_t cs, cl;
                if (canOpen && FindCloser(s, q, c, cs, cl)) {
                    size_t k = std::min(run, cl);
                    if (k > 3) k = 3;
                    buf.append(run - k, c);
                    flush();
                    unsigned add = k == 1 ? F_ITALIC : k == 2 ? F_BOLD : (F_BOLD | F_ITALIC);
                    ParseRange(s.substr(q, cs - q), flags | add, url);
                    refresh();
                    p = cs + k;
                    continue;
                }
                buf.append(run, c); p = q;
                continue;
            }
            case '~':
            case '=': {
                if (p + 1 < n && s[p + 1] == c && !(p + 2 < n && s[p + 2] == c)) {
                    size_t e = s.find(c == '~' ? L"~~" : L"==", p + 2);
                    if (e != WS::npos && e > p + 2 && !IsWs(s[p + 2]) && !IsWs(s[e - 1])) {
                        flush();
                        ParseRange(s.substr(p + 2, e - p - 2), flags | (c == '~' ? F_STRIKE : F_MARK), url);
                        refresh();
                        p = e + 2;
                        continue;
                    }
                }
                buf += c; p++;
                continue;
            }
            case '&': {
                size_t e;
                if (DecodeEntity(s, p, buf, e)) { p = e; continue; }
                buf += c; p++;
                continue;
            }
            case '\n': {
                size_t sp = 0;
                while (sp < buf.size() && buf[buf.size() - 1 - sp] == ' ') sp++;
                bool hard = sp >= 2;
                buf.erase(buf.size() - sp);
                p++;
                while (p < n && s[p] == ' ') p++;
                if (hard) { flush(); EmitBreak(); }
                else buf += ' ';
                continue;
            }
            case 'h': case 'H': case 'w': case 'W': {
                if (!(curFlags & F_LINK) && (p == 0 || !IsAlnum(s[p - 1])) &&
                    (StartsWithCI(s, p, L"http://") || StartsWithCI(s, p, L"https://") || StartsWithCI(s, p, L"www."))) {
                    size_t e = p;
                    while (e < n && !IsWs(s[e]) && s[e] != '<') e++;
                    // trim trailing punctuation
                    while (e > p) {
                        wchar_t t = s[e - 1];
                        if (t == '.' || t == ',' || t == ':' || t == ';' || t == '!' || t == '?' || t == '"' ||
                            t == '\'' || t == '*' || t == '_' || t == '~') { e--; continue; }
                        if (t == ')') {
                            int open = 0, close = 0;
                            for (size_t k = p; k < e; k++) { if (s[k] == '(') open++; else if (s[k] == ')') close++; }
                            if (close > open) { e--; continue; }
                        }
                        break;
                    }
                    WS u = s.substr(p, e - p);
                    size_t hostStart = StartsWithCI(u, 0, L"www.") ? 4 : u.find(L"//") + 2;
                    if (u.size() > hostStart + 1) {
                        flush();
                        EmitText(u, curFlags | F_LINK, StartsWithCI(u, 0, L"www.") ? L"http://" + u : u);
                        p = e;
                        continue;
                    }
                }
                buf += c; p++;
                continue;
            }
            default:
                buf += c; p++;
                continue;
            }
        }
        flush();
    }
};

// ---------------------------------------------------------------- block parser

class BlockParser {
public:
    RefMap refs;

    std::vector<Block> ParseBlocks(const Lines& L, bool topLevel) {
        std::vector<Block> out;
        size_t i = 0, n = L.size();
        if (depth_ >= kMaxBlockDepth) {   // pathological nesting: show the rest verbatim
            Block b; b.type = BlockType::CodeBlock;
            for (size_t k = 0; k < n; k++) { if (k) b.text += '\n'; b.text += L[k]; }
            out.push_back(b);
            return out;
        }
        struct DepthGuard { int& d; DepthGuard(int& x) : d(x) { d++; } ~DepthGuard() { d--; } } guard(depth_);

        // YAML front matter
        if (topLevel && n > 1 && TrimRight(L[0]) == L"---") {
            for (size_t j = 1; j < n && j < 200; j++) {
                WS t = TrimRight(L[j]);
                if (t == L"---" || t == L"...") {
                    if (j > 1) {
                        Block b; b.type = BlockType::FrontMatter;
                        for (size_t k = 1; k < j; k++) { if (k > 1) b.text += '\n'; b.text += L[k]; }
                        b.info = L"yaml";
                        out.push_back(b);
                        i = j + 1;
                    }
                    break;
                }
            }
        }

        while (i < n) {
            const WS& line = L[i];
            if (IsBlank(line)) { i++; continue; }
            int ind = Indent(line);

            // indented code
            if (ind >= 4) {
                Lines cl;
                while (i < n && (IsBlank(L[i]) || Indent(L[i]) >= 4)) {
                    cl.push_back(L[i].size() > 4 ? L[i].substr(4) : WS());
                    i++;
                }
                while (!cl.empty() && IsBlank(cl.back())) cl.pop_back();
                Block b; b.type = BlockType::CodeBlock;
                for (size_t k = 0; k < cl.size(); k++) { if (k) b.text += '\n'; b.text += cl[k]; }
                out.push_back(b);
                continue;
            }

            // fenced code
            {
                wchar_t ch; int len, find; WS info;
                if (FenceStart(line, ch, len, info, find)) {
                    i++;
                    Block b; b.type = BlockType::CodeBlock; b.info = info;
                    bool first = true;
                    while (i < n) {
                        if (IsFenceClose(L[i], ch, len)) { i++; break; }
                        const WS& l = L[i];
                        int k = 0; while (k < find && k < (int)l.size() && l[k] == ' ') k++;
                        if (!first) b.text += '\n';
                        b.text += l.substr(k);
                        first = false;
                        i++;
                    }
                    out.push_back(b);
                    continue;
                }
            }

            // ATX heading
            {
                int level; WS content;
                if (AtxHeading(line, level, content)) {
                    Block b; b.type = BlockType::Heading; b.level = level; b.text = content;
                    out.push_back(b);
                    i++;
                    continue;
                }
            }

            // thematic break
            if (IsHr(line)) {
                Block b; b.type = BlockType::Rule;
                out.push_back(b);
                i++;
                continue;
            }

            // block quote
            if (IsQuoteStart(line)) {
                Lines ql;
                while (i < n) {
                    const WS& l = L[i];
                    if (IsBlank(l)) break;
                    int qi = Indent(l);
                    if (qi < 4 && l[qi] == '>') {
                        WS r = l.substr(qi + 1);
                        if (!r.empty() && r[0] == ' ') r = r.substr(1);
                        ql.push_back(r);
                        i++;
                        continue;
                    }
                    if (!ql.empty() && !IsBlank(ql.back()) && !CanInterrupt(l) && !FenceOpenAtEnd(ql)) {
                        ql.push_back(l);
                        i++;
                        continue;
                    }
                    break;
                }
                Block b; b.type = BlockType::Quote;
                b.children = ParseBlocks(ql, false);
                out.push_back(b);
                continue;
            }

            // HTML comment block (hidden)
            if (line.compare(ind, 4, L"<!--") == 0) {
                size_t j = i;
                bool closed = false;
                while (j < n) {
                    size_t from = (j == i) ? ind + 4 : 0;
                    size_t e = L[j].find(L"-->", from);
                    if (e != WS::npos) {
                        closed = true;
                        WS rest = Trim(L[j].substr(e + 3));
                        j++;
                        if (!rest.empty()) {  // text after the comment: treat as html block line
                            Block b; b.type = BlockType::Html; b.text = rest;
                            out.push_back(b);
                        }
                        break;
                    }
                    j++;
                }
                if (closed) { i = j; continue; }
                // unclosed comment: hide rest of the block until blank line
                while (i < n && !IsBlank(L[i])) i++;
                continue;
            }

            // HTML block
            if (IsHtmlBlockStart(line, false)) {
                Block b; b.type = BlockType::Html;
                bool first = true;
                while (i < n && !IsBlank(L[i])) {
                    if (!first) b.text += '\n';
                    b.text += TrimLeft(L[i]);
                    first = false;
                    i++;
                }
                out.push_back(b);
                continue;
            }

            // list
            {
                ListMarkerInfo m0;
                if (ListMarker(line, m0)) {
                    out.push_back(ParseList(L, i));
                    continue;
                }
            }

            // table
            if (TableStartsAt(L, i)) {
                Block b; b.type = BlockType::Table;
                IsDelimRow(L[i + 1], b.aligns);
                size_t cols = b.aligns.size();
                std::vector<WS> hdr = SplitRow(L[i]);
                hdr.resize(cols);
                b.rawRows.push_back(hdr);
                i += 2;
                while (i < n && !IsBlank(L[i]) && L[i].find('|') != WS::npos && !CanInterrupt(L[i])) {
                    std::vector<WS> row = SplitRow(L[i]);
                    row.resize(cols);
                    b.rawRows.push_back(row);
                    i++;
                }
                out.push_back(b);
                continue;
            }

            // link reference definition
            {
                WS label, url, title;
                if (RefDef(line, label, url, title)) {
                    WS key = NormLabel(label);
                    if (refs.find(key) == refs.end()) { LinkRef r; r.url = url; r.title = title; refs[key] = r; }
                    i++;
                    continue;
                }
            }

            // paragraph (possibly setext heading)
            {
                Lines pl;
                pl.push_back(TrimLeft(line));
                i++;
                int setext = 0;
                while (i < n) {
                    const WS& l2 = L[i];
                    if (IsBlank(l2)) break;
                    if (Indent(l2) < 4) {
                        WS t = Trim(l2);
                        if (!t.empty() && t.find_first_not_of('=') == WS::npos) { setext = 1; i++; break; }
                        if (!t.empty() && t.find_first_not_of('-') == WS::npos) { setext = 2; i++; break; }
                        if (CanInterrupt(l2)) break;
                        if (TableStartsAt(L, i)) break;
                    }
                    pl.push_back(TrimLeft(l2));
                    i++;
                }
                Block b;
                for (size_t k = 0; k < pl.size(); k++) { if (k) b.text += '\n'; b.text += pl[k]; }
                b.text = TrimRight(b.text);
                if (setext) { b.type = BlockType::Heading; b.level = setext; }
                else b.type = BlockType::Paragraph;
                out.push_back(b);
            }
        }
        return out;
    }

    // Converts raw text to inline spans once all reference definitions are known
    void ResolveInlines(Block& b) {
        InlineParser ip(refs);
        switch (b.type) {
        case BlockType::Paragraph:
        case BlockType::Html:
            b.inl = ip.Parse(b.text);
            break;
        case BlockType::Heading: {
            b.inl = ip.Parse(b.text);
            WS slug = Slugify(PlainText(b.inl));
            int& cnt = slugCount_[slug];
            b.anchor = cnt ? slug + L"-" + std::to_wstring(cnt) : slug;
            cnt++;
            break;
        }
        case BlockType::Table:
            b.rows.clear();
            for (size_t r = 0; r < b.rawRows.size(); r++) {
                std::vector<Inlines> row;
                for (size_t c = 0; c < b.rawRows[r].size(); c++) row.push_back(ip.Parse(b.rawRows[r][c]));
                b.rows.push_back(row);
            }
            break;
        default:
            break;
        }
        for (size_t k = 0; k < b.children.size(); k++) ResolveInlines(b.children[k]);
    }

private:
    std::map<WS, int> slugCount_;
    int depth_ = 0;
    enum { kMaxBlockDepth = 24 };

    static bool CanInterrupt(const WS& l) {
        if (IsBlank(l)) return true;
        int lv; WS c;
        if (AtxHeading(l, lv, c)) return true;
        wchar_t ch; int len, ind; WS info;
        if (FenceStart(l, ch, len, info, ind)) return true;
        if (IsHr(l)) return true;
        if (IsQuoteStart(l)) return true;
        if (IsHtmlBlockStart(l, true)) return true;
        ListMarkerInfo m;
        if (ListMarker(l, m) && !m.empty && (!m.ordered || m.number == 1)) return true;
        return false;
    }

    static bool SameListType(const ListMarkerInfo& a, const ListMarkerInfo& b) {
        return a.ordered == b.ordered && a.ch == b.ch;
    }

    Block ParseList(const Lines& L, size_t& i) {
        size_t n = L.size();
        ListMarkerInfo m0;
        ListMarker(L[i], m0);
        Block list; list.type = BlockType::List;
        list.ordered = m0.ordered; list.start = m0.number;
        bool loose = false;

        while (i < n) {
            ListMarkerInfo m;
            if (!ListMarker(L[i], m) || !SameListType(m, m0) || IsHr(L[i])) break;
            Lines il;
            const WS& first = L[i];
            il.push_back(m.empty ? WS() : ((int)first.size() > m.contentIndent ? first.substr(m.contentIndent) : WS()));
            int W = m.contentIndent;
            i++;
            while (i < n) {
                const WS& l = L[i];
                if (IsBlank(l)) {
                    if (m.empty && il.size() == 1) break;   // empty item followed by blank line ends it
                    il.push_back(WS());
                    i++;
                    continue;
                }
                int ind = Indent(l);
                if (ind >= W) { il.push_back(l.substr(W)); i++; continue; }
                ListMarkerInfo mm;
                bool prevBlank = IsBlank(il.back());
                if (!prevBlank && !CanInterrupt(l) && !ListMarker(l, mm) && !FenceOpenAtEnd(il)) {
                    il.push_back(TrimLeft(l));  // lazy continuation
                    i++;
                    continue;
                }
                break;
            }
            int trailing = 0;
            while (il.size() > 1 && IsBlank(il.back())) { il.pop_back(); trailing++; }

            // blank line between direct children of the item -> loose
            {
                bool inFence = false; wchar_t fch = 0; int flen = 0;
                for (size_t k = 0; k + 1 < il.size(); k++) {
                    WS info; int find; wchar_t c2; int l2;
                    if (!inFence && FenceStart(il[k], c2, l2, info, find)) { inFence = true; fch = c2; flen = l2; continue; }
                    if (inFence) { if (IsFenceClose(il[k], fch, flen)) inFence = false; continue; }
                    if (IsBlank(il[k]) && !IsBlank(il[k + 1]) && Indent(il[k + 1]) == 0 && k > 0) loose = true;
                }
            }

            Block item; item.type = BlockType::ListItem;
            WS& f = il[0];
            if (f.size() >= 3 && f[0] == '[' && f[2] == ']' && (f[1] == ' ' || f[1] == 'x' || f[1] == 'X') &&
                (f.size() == 3 || f[3] == ' ')) {
                item.task = f[1] == ' ' ? 0 : 1;
                f = f.size() > 4 ? f.substr(4) : WS();
            }
            item.children = ParseBlocks(il, false);
            list.children.push_back(item);

            if (i >= n) break;
            if (trailing > 0) {
                ListMarkerInfo m2;
                if (ListMarker(L[i], m2) && SameListType(m2, m0) && !IsHr(L[i])) loose = true;
                else break;
            }
        }
        list.tight = !loose;
        return list;
    }
};

} // namespace

// ---------------------------------------------------------------- public API

std::wstring PlainText(const Inlines& inl) {
    std::wstring r;
    for (size_t i = 0; i < inl.size(); i++) {
        if (inl[i].lineBreak) r += ' ';
        else r += inl[i].text;
    }
    return r;
}

std::wstring Slugify(const std::wstring& text) {
    std::wstring r;
    for (size_t i = 0; i < text.size(); i++) {
        wchar_t c = text[i];
        if (c == ' ') r += '-';
        else if (c == '-' || c == '_') r += c;
        else if (IsAlnum(c)) r += LowerCh(c);
    }
    return r;
}

Block Parse(const std::wstring& text) {
    Lines lines;
    std::wstring cur;
    for (size_t i = 0; i < text.size(); i++) {
        wchar_t c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') i++;
            lines.push_back(ExpandTabs(cur)); cur.clear();
        } else if (c == '\n') {
            lines.push_back(ExpandTabs(cur)); cur.clear();
        } else if (c == 0xFEFF && i == 0) {
            // BOM
        } else if (c == 0) {
            cur += (wchar_t)0xFFFD;
        } else cur += c;
    }
    if (!cur.empty()) lines.push_back(ExpandTabs(cur));

    BlockParser bp;
    Block doc; doc.type = BlockType::Document;
    doc.children = bp.ParseBlocks(lines, true);
    bp.ResolveInlines(doc);
    return doc;
}

} // namespace md
