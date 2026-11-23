// Native (non-Windows) test driver: parses a Markdown file and writes HTML + RTF output.
#include "../src/md_parser.h"
#include "../src/md_rtf.h"
#include "../src/md_html.h"
#include <cstdio>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

static std::wstring FromUtf8(const std::string& s) {
    std::wstring r; size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i]; unsigned long cp; int n;
        if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; } else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        r += (wchar_t)cp; i += n;
    }
    return r;
}
static std::string ToUtf8(const std::wstring& w) {
    std::string r;
    for (wchar_t wc : w) { unsigned long c = wc;
        if (c < 0x80) r += (char)c;
        else if (c < 0x800) { r += (char)(0xC0 | (c >> 6)); r += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { r += (char)(0xE0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 0x3F)); r += (char)(0x80 | (c & 0x3F)); }
        else { r += (char)(0xF0 | (c >> 18)); r += (char)(0x80 | ((c >> 12) & 0x3F)); r += (char)(0x80 | ((c >> 6) & 0x3F)); r += (char)(0x80 | (c & 0x3F)); } }
    return r;
}

static int fails = 0;
static void Check(const std::string& md, const std::string& expectHtmlFragment) {
    md::Block d = md::Parse(FromUtf8(md));
    std::string html = ToUtf8(md::RenderHtml(d, L"", false));
    if (html.find(expectHtmlFragment) == std::string::npos) {
        fails++;
        std::cout << "FAIL: " << md << "\n  expected: " << expectHtmlFragment << "\n  got: " << html << "\n";
    }
}

int main(int argc, char** argv) {
    Check("**bold**", "<p><strong>bold</strong></p>");
    Check("*a **b** c*", "<em>a </em><strong><em>b</em></strong><em> c</em>");
    Check("**bold *italic***", "<strong>bold </strong><strong><em>italic</em></strong>");
    Check("snake_case_word", "<p>snake_case_word</p>");
    Check("`a*b*c`", "<code>a*b*c</code>");
    Check("# H1 #", "<h1 id=\"h1\">H1</h1>");
    Check("Title\n---", "<h2 id=\"title\">Title</h2>");
    Check("- a\n- b", "<ul>\n<li>a\n</li>\n<li>b\n</li>\n</ul>");
    Check("1. a\n2. b", "<ol>");
    Check("5. a", "<ol start=\"5\">");
    Check("- [x] done", "checked");
    Check("[t](http://x.y)", "<a href=\"http://x.y\">t</a>");
    Check("[t][r]\n\n[r]: http://r.r", "<a href=\"http://r.r\">t</a>");
    Check("see https://a.b/c_d.", "<a href=\"https://a.b/c_d\">https://a.b/c_d</a>.");
    Check("|a|b|\n|-|:-:|\n|1|2|", "<th>a</th><th style=\"text-align:center\">b</th>");
    Check("> q\n> > n", "<blockquote>\n<p>q</p>\n<blockquote>");
    Check("```js\nx<1\n```", "<pre><code class=\"language-js\">x&lt;1\n</code></pre>");
    Check("    code", "<pre><code>code\n</code></pre>");
    Check("a  \nb", "a<br>\nb");
    Check("***", "<hr>");
    Check("- a\n\n- b", "<li><p>a</p>\n</li>");
    Check("&copy; &#65;", "\xC2\xA9 A");
    Check("~~s~~", "<del>s</del>");
    Check("<b>x</b> y", "<strong>x</strong> y");
    Check("a\n- b", "<p>a</p>\n<ul>");
    Check("a\n2. b", "<p>a 2. b</p>");
    Check("x <!-- c --> y", "x  y");
    Check("\\*lit\\*", "<p>*lit*</p>");
    Check("[![i](a.png)](http://l)", "<a href=\"http://l\"><span class=\"img\"");

    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        std::wstring src = FromUtf8(ss.str());
        md::Block d = md::Parse(src);
        std::ofstream(std::string(argv[1]) + ".html") << ToUtf8(md::RenderHtml(d, L"Sample"));
        md::RtfStyle st;
        md::RtfOutput ro = md::RenderRtf(d, st);
        std::ofstream(std::string(argv[1]) + ".rtf") << ro.rtf;
        std::ofstream(std::string(argv[1]) + ".src.rtf") << md::RenderSourceRtf(src, st);
        std::cout << "links: " << ro.links.size() << " headings: " << ro.headings.size() << "\n";
    }
    std::cout << (fails ? "FAILURES: " : "all passed ") << fails << "\n";
    return fails ? 1 : 0;
}
