// text_load.cpp - read a text file into UTF-16, detecting the encoding
#include "text_load.h"
#include <vector>

bool LoadTextFile(const std::wstring& path, std::wstring& out, unsigned long maxBytes) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart > (LONGLONG)maxBytes) { CloseHandle(f); return false; }
    DWORD len = (DWORD)size.QuadPart;
    std::vector<unsigned char> buf(len + 2, 0);
    DWORD got = 0;
    BOOL ok = len == 0 || ReadFile(f, &buf[0], len, &got, NULL);
    CloseHandle(f);
    if (!ok) return false;
    len = got;
    const unsigned char* p = &buf[0];

    // UTF-16 with BOM
    if (len >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        out.assign((const wchar_t*)(p + 2), (len - 2) / 2);
        return true;
    }
    if (len >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        size_t n = (len - 2) / 2;
        out.resize(n);
        for (size_t i = 0; i < n; i++) out[i] = (wchar_t)((p[2 + 2 * i] << 8) | p[3 + 2 * i]);
        return true;
    }
    // UTF-8 (with or without BOM), else the ANSI code page
    if (len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) { p += 3; len -= 3; }
    if (len == 0) return true;
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCSTR)p, (int)len, NULL, 0);
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)p, (int)len, NULL, 0);
        if (n <= 0) return false;
    }
    out.resize(n);
    MultiByteToWideChar(cp, 0, (LPCSTR)p, (int)len, &out[0], n);
    return true;
}
