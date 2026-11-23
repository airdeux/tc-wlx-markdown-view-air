// lister.h - controller: TC-facing container window, keyboard routing, modes, settings
#pragma once
#include <windows.h>
#include <string>

void Lister_Init(HINSTANCE hinst);
void Lister_SetIniFile(const std::wstring& iniFile);
bool Lister_IsSupportedFile(const wchar_t* fileName);
std::string Lister_DetectString();

// Creates the lister window for a file; returns NULL if the file cannot be shown.
HWND Lister_Open(HWND parent, const wchar_t* fileName, int showFlags);
bool Lister_LoadNext(HWND listWin, const wchar_t* fileName, int showFlags);
int  Lister_SendCommand(HWND listWin, int command, int parameter);
int  Lister_SearchText(HWND listWin, const wchar_t* text, int searchParameter);
int  Lister_Print(HWND listWin, const wchar_t* fileName, const RECT* margins);
