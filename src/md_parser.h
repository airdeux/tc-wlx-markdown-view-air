// md_parser.h - renderer-independent Markdown parser (CommonMark subset + GFM extensions)
//
// The parser produces a small document tree (md::Block / md::Span). Output back-ends
// (RTF for the RichEdit view, HTML for a future WebView2 view) only consume this tree,
// so they can be swapped without touching the parsing code.
//
// No Windows dependencies: this file compiles on any platform (used by the unit tests).

#pragma once
#include <string>
#include <vector>
#include <map>

namespace md {

// Inline style flags (combinable)
enum InlineFlags {
    F_BOLD      = 0x0001,
    F_ITALIC    = 0x0002,
    F_STRIKE    = 0x0004,
    F_CODE      = 0x0008,
    F_LINK      = 0x0010,
    F_IMAGE     = 0x0020,   // text = alt text, url = image source
    F_UNDERLINE = 0x0040,
    F_KBD       = 0x0080,
    F_SUP       = 0x0100,
    F_SUB       = 0x0200,
    F_MARK      = 0x0400
};

struct Span {
    std::wstring text;
    unsigned     flags = 0;
    std::wstring url;          // link target (F_LINK) or image source (F_IMAGE)
    bool         lineBreak = false;   // hard line break; text is empty
};
typedef std::vector<Span> Inlines;

enum class BlockType {
    Document, Paragraph, Heading, CodeBlock, Quote, List, ListItem, Rule, Table, Html, FrontMatter
};

enum class Align { Default, Left, Center, Right };

struct Block {
    BlockType type = BlockType::Paragraph;
    int  level = 0;                 // Heading: 1..6
    Inlines inl;                    // Paragraph, Heading, Html (stripped)
    std::wstring text;              // CodeBlock / FrontMatter content, raw text before inline parsing
    std::wstring info;              // CodeBlock: fence info string (language)
    std::wstring anchor;            // Heading: GitHub-style slug
    std::vector<Block> children;    // Document, Quote, List, ListItem
    bool ordered = false;           // List
    int  start = 1;                 // List: first number
    bool tight = true;              // List
    int  task = -1;                 // ListItem: -1 = none, 0 = [ ], 1 = [x]
    std::vector<Align> aligns;                      // Table
    std::vector<std::vector<std::wstring> > rawRows; // Table (raw cell text)
    std::vector<std::vector<Inlines> > rows;         // Table, rows[0] = header
};

// Parse a complete document. Line endings may be \n, \r\n or \r.
Block Parse(const std::wstring& text);

// Plain text of inlines (used for alt text, anchors, table sizing ...)
std::wstring PlainText(const Inlines& inl);

// GitHub-compatible heading slug
std::wstring Slugify(const std::wstring& text);

} // namespace md
