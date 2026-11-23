// text_load.h - read a text file into UTF-16 (BOM / UTF-8 / ANSI detection)
#pragma once
#include <windows.h>
#include <string>

// Returns false if the file cannot be read or is larger than maxBytes.
bool LoadTextFile(const std::wstring& path, std::wstring& out, unsigned long maxBytes);
