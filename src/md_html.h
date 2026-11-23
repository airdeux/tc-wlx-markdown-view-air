// md_html.h - document tree -> HTML
//
// Not used by the RichEdit back-end. It exists so that a future WebView2 (Edge)
// back-end can render the very same parse tree, and it is used by the unit tests
// to inspect the parser output in a readable form.
#pragma once
#include "md_parser.h"
#include <string>

namespace md {

// Returns a complete, self-contained HTML5 document (UTF-16; convert to UTF-8 as needed).
std::wstring RenderHtml(const Block& doc, const std::wstring& title, bool fullDocument = true);

} // namespace md
