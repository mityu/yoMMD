#ifndef WINDOWS_MSGBOX_HPP_
#define WINDOWS_MSGBOX_HPP_

#include "winver.hpp"
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <string_view>

class MsgBox {
public:
    static void Init();
    static void Terminate();
    static void Show(std::string_view);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void drawContents(HWND hwnd);

private:
    constexpr static PCWSTR className_ = L"yoMMD-messagebox";
    constexpr static int okMenuID_ = 100;
    constexpr static UINT winStyle_ = WS_CAPTION | WS_SYSMENU | WS_TABSTOP | WS_DLGFRAME;
    static bool initialized_;
    static bool showingWindow_;
    static HINSTANCE hInstance_;
    static HFONT hfont_;
    static HWND buttonHWND_;
    static std::wstring wmsg_;
};

#endif  // WINDOWS_MSGBOX_HPP_
