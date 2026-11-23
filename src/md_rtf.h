// md_rtf.h - document tree -> RTF (for the RichEdit view)
#pragma once
#include "md_parser.h"
#include <string>
#include <vector>

namespace md {

// Background colour matching the colour scheme (0x00BBGGRR, Win32 COLORREF layout)
unsigned long RtfBackground(bool dark);

struct RtfStyle {
    std::wstring bodyFont = L"Segoe UI";
    std::wstring codeFont = L"Consolas";
    std::wstring symbolFont = L"Segoe UI Symbol";
    int  baseSize = 11;            // body font size in points
    int  codeSize = 10;            // code font size in points
    int  pageWidthTwips = 9000;    // usable text width (set by the view from its client width)
    bool sourceHighlight = true;   // light syntax colouring in source mode
    bool dark = false;             // dark colour scheme (TC dark mode)
};

struct HeadingRef { std::wstring text; std::wstring anchor; int level; };
struct LinkRef    { std::wstring text; std::wstring url; };

struct RtfOutput {
    std::string rtf;                       // pure 7-bit ASCII RTF
    std::vector<HeadingRef> headings;      // in document order (for #anchor links)
    std::vector<LinkRef> links;            // in document order (to resolve EN_LINK clicks)
};

// Rich ("preview") rendering
RtfOutput RenderRtf(const Block& doc, const RtfStyle& style);

// Source view: the Markdown text itself in the code font (optionally lightly coloured)
std::string RenderSourceRtf(const std::wstring& source, const RtfStyle& style);

} // namespace md
