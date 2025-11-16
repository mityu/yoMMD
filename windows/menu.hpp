#ifndef WINDOWS_MENU_HPP_
#define WINDOWS_MENU_HPP_

#include "winver.hpp"
#include <windows.h>
#include <windowsx.h>
#include <type_traits>
#include "../util.hpp"

class AppMenu {
public:
    AppMenu();
    ~AppMenu();
    void Setup();
    void Terminate();
    void ShowMenu();
    bool IsMenuOpened() const;

public:
    static constexpr UINT YOMMD_WM_TOGGLE_ENABLE_MOUSE = WM_APP;
    static constexpr UINT YOMMD_WM_SHOW_TASKBAR_MENU = WM_APP + 1;

private:
    static DWORD WINAPI showMenu(LPVOID param);
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void createTaskbar();

private:
    class Cmd {
    private:
        template <typename LHS, typename RHS>
        using SelectSmallerSizeType = std::conditional<sizeof(LHS) < sizeof(RHS), LHS, RHS>;

        template <typename HD, typename... TL>
        struct MinimumSizeType {
            using type =
                typename SelectSmallerSizeType<HD, typename MinimumSizeType<TL...>::type>::
                    type;
        };

        template <typename T>
        struct MinimumSizeType<T> {
            using type = T;
        };

    public:
        using UnderlyingType = MinimumSizeType<UINT_PTR, UINT, WORD>::type;
        enum class Kind : UnderlyingType {
            None,
            EnableMouse,
            ResetPosition,
            SelectScreen,
            HideWindow,
            Quit,
            MenuCount,
        };
        using enum Kind;

        static constexpr Kind GetCmd(UnderlyingType cmd);
        static constexpr UnderlyingType GetUserData(UnderlyingType cmd);
        static constexpr UnderlyingType Combine(Kind cmd, UnderlyingType userData);

    private:
        static constexpr size_t fieldLength_ = sizeof(UnderlyingType) * 8 / 2;

        static_assert(
            Enum::underlyCast(Kind::MenuCount) < (UnderlyingType(1) << fieldLength_),
            "Too many menu commands declared");
    };

    static constexpr PCWSTR wcMenuName = L"yoMMD-menu-window";
    static constexpr PCWSTR wcSelectorName = L"yoMMD-screen-selector-window";

    HANDLE hMenuThread_;
    HICON hTaskbarIcon_;
    NOTIFYICONDATAW taskbarIconDesc_;
    bool isMenuOpened_;
};

#endif  // WINDOWS_MENU_HPP_
