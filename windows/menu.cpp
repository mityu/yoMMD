#include "menu.hpp"
#include <dwmapi.h>
#include "main.hpp"

namespace {
template <typename T, typename DeleteFunc, DeleteFunc deleteFunc>
class UniqueHandler {
public:
    UniqueHandler(T handler) : handler_(handler, deleteFunc) {};
    UniqueHandler() : UniqueHandler(nullptr) {};
    T GetRawHandler() { return handler_.get(); };
    operator T() { return GetRawHandler(); };
    UniqueHandler& operator=(T& handler) {
        handler_.reset(handler);
        return *this;
    };

private:
    std::unique_ptr<std::remove_pointer_t<T>, DeleteFunc> handler_;
};

using UniqueHWND = UniqueHandler<HWND, decltype(&DestroyWindow), &DestroyWindow>;
using UniqueHMENU = UniqueHandler<HMENU, decltype(&DestroyMenu), &DestroyMenu>;
}  // namespace

AppMenu::AppMenu() : hMenuThread_(nullptr), hTaskbarIcon_(nullptr) {}

AppMenu::~AppMenu() {
    Terminate();
}

void AppMenu::Setup() {
    WNDCLASSW wc = {};

    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = AppMenu::windowProc, wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = wcMenuName;
    wc.hIcon = LoadIcon(nullptr, IDI_WINLOGO);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = wcSelectorName;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&wc);

    createTaskbar();
}

void AppMenu::Terminate() {
    if (hTaskbarIcon_) {
        DestroyIcon(hTaskbarIcon_);
        hTaskbarIcon_ = nullptr;
    }

    Shell_NotifyIconW(NIM_DELETE, &taskbarIconDesc_);
    UnregisterClassW(wcMenuName, GetModuleHandleW(nullptr));

    DWORD exitCode;
    if (GetExitCodeThread(hMenuThread_, &exitCode) && exitCode == STILL_ACTIVE) {
        Info::Log("Thread is still running. Wait finishing.");
        // Right click menu/toolbar menu must not appear so long.
        // We should be able to wait for it is closed.
        WaitForSingleObject(hMenuThread_, INFINITE);
        CloseHandle(hMenuThread_);
    }
}

void AppMenu::ShowMenu() {
    DWORD exitCode;
    if (GetExitCodeThread(hMenuThread_, &exitCode) && exitCode == STILL_ACTIVE) {
        Info::Log("Thread is running");
    }

    hMenuThread_ = CreateThread(NULL, 0, AppMenu::showMenu, this, 0, NULL);
}

bool AppMenu::IsMenuOpened() const {
    return isMenuOpened_;
}

DWORD WINAPI AppMenu::showMenu(LPVOID param) {
    AppMenu *appMenu = reinterpret_cast<AppMenu *>(param);
    constexpr DWORD winStyle = WS_CHILD;
    const HWND& parentWin = getAppMain().GetWindowHandle();
    UniqueHWND hMenuWindow, hSelectorWindow;

    const LONG parentWinExStyle = GetWindowLongW(parentWin, GWL_EXSTYLE);
    if (parentWinExStyle == 0) {
        Info::Log("Failed to get parent window's style");
    }

    hSelectorWindow = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE, wcSelectorName, L"", WS_DISABLED | WS_POPUP, 0, 0, 0,
        0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hSelectorWindow.GetRawHandler()) {
        Err::Log("Failed to create screen selector window.");
        return 1;
    }
    SetLayeredWindowAttributes(hSelectorWindow, RGB(0, 0, 0), 127, LWA_ALPHA);
    BOOL fDisable = TRUE;
    DwmSetWindowAttribute(
        hSelectorWindow, DWMWA_TRANSITIONS_FORCEDISABLED, &fDisable, sizeof(fDisable));

    hMenuWindow = CreateWindowExW(
        0, wcMenuName, L"", winStyle, 0, 0, 0, 0, parentWin, nullptr,
        GetModuleHandleW(nullptr), hSelectorWindow);
    if (!hMenuWindow.GetRawHandler()) {
        Err::Log("Failed to create dummy window for menu.");
        return 1;
    }

    POINT point;
    if (!GetCursorPos(&point)) {
        Err::Log("Failed to get mouse point");
        return 1;
    }

    UniqueHMENU hScreensMenu = CreatePopupMenu();
    const HMONITOR curMonitorHandle =
        MonitorFromWindow(getAppMain().GetWindowHandle(), MONITOR_DEFAULTTONULL);
    std::vector<HMONITOR> monitorHandles = getAllMonitorHandles();
    for (int cnt = monitorHandles.size(), i = 0; i < cnt; ++i) {
        const std::wstring title(L"&Screen" + std::to_wstring(i));
        const auto op = Cmd::Combine(Cmd::SelectScreen, i);
        AppendMenuW(hScreensMenu, MF_STRING, op, title.c_str());
        if (monitorHandles[i] == curMonitorHandle) {
            EnableMenuItem(hScreensMenu, op, MF_DISABLED);
        }
    }

    UniqueHMENU hmenu = CreatePopupMenu();
    if (parentWinExStyle & WS_EX_TRANSPARENT) {
        AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::EnableMouse), L"&Enable Mouse");
    } else {
        AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::EnableMouse), L"&Disable Mouse");
    }
    AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::ResetPosition), L"&Reset Position");
    AppendMenuW(hmenu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(
        hmenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hScreensMenu.GetRawHandler()),
        L"&Select screen");
    AppendMenuW(hmenu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    if (GetWindowLongPtrW(parentWin, GWL_STYLE) & WS_VISIBLE)
        AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::HideWindow), L"&Hide Window");
    else
        AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::HideWindow), L"&Show Window");
    AppendMenuW(hmenu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::Quit), L"&Quit");

    if (parentWinExStyle == 0) {
        EnableMenuItem(hmenu, Enum::underlyCast(Cmd::EnableMouse), MF_DISABLED);
    }

    if (monitorHandles.size() <= 1)
        EnableMenuItem(
            hmenu, reinterpret_cast<UINT_PTR>(hScreensMenu.GetRawHandler()), MF_DISABLED);

    constexpr UINT menuFlags = TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD;

    SetForegroundWindow(hMenuWindow);
    appMenu->isMenuOpened_ = true;
    const auto op = TrackPopupMenuEx(hmenu, menuFlags, point.x, point.y, hMenuWindow, nullptr);
    appMenu->isMenuOpened_ = false;

    switch (Cmd::GetCmd(op)) {
    case Cmd::EnableMouse:
        if (parentWinExStyle != 0) {
            SetWindowLongW(parentWin, GWL_EXSTYLE, parentWinExStyle ^ WS_EX_TRANSPARENT);
        }
        break;
    case Cmd::ResetPosition:
        getAppMain().GetRoutine().ResetModelPosition();
        break;
    case Cmd::SelectScreen:
        getAppMain().ChangeScreen(Cmd::GetUserData(op));
        break;
    case Cmd::HideWindow:
        if (GetWindowLongPtrW(parentWin, GWL_STYLE) & WS_VISIBLE)
            ShowWindow(parentWin, SW_HIDE);
        else
            ShowWindow(parentWin, SW_SHOWNORMAL);
        break;
    case Cmd::Quit:
        SendMessageW(parentWin, WM_DESTROY, 0, 0);
        break;
    case Cmd::None:
        // Menu is canceled.
        // The mouse click to cancel right-click menu will dispatch
        // WM_MOUSEMOVE message and it leads wrong and fake mouse drag event.
        // As a workaround, in order to avoid this call OnGestureEnd to update
        // the action context.
        getAppMain().GetRoutine().OnGestureEnd();
        break;
    case Cmd::MenuCount:
        Err::Log("Internal error: Command::MenuCount is used");
        break;
    }

    return 0;
}

LRESULT CALLBACK AppMenu::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_MENUSELECT:
        if (HIWORD(wParam) & MF_MOUSESELECT && !(HIWORD(wParam) & MF_POPUP)) {
            const HWND hSelectorWindow =
                reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            ShowWindow(hSelectorWindow, SW_HIDE);

            const HMENU hmenu = reinterpret_cast<HMENU>(lParam);
            const std::underlying_type_t<Cmd::Kind> op = LOWORD(wParam);
            MENUITEMINFOW itemInfo = {
                .cbSize = sizeof(itemInfo),
                .fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID,
            };
            GetMenuItemInfoW(hmenu, op, false, &itemInfo);
            if (itemInfo.fType != MFT_STRING || itemInfo.fState & MFS_DISABLED)
                break;
            if (Cmd::GetCmd(op) != Cmd::SelectScreen)
                break;

            const auto r = getMonitorWorkareaFromID(Cmd::GetUserData(op));
            if (!r.has_value()) {
                // Maybe monitor is disconnected. Do nothing.
                break;
            }

            const auto cx = r->right - r->left;
            const auto cy = r->bottom - r->top;
            constexpr UINT uFlags = SWP_SHOWWINDOW | SWP_NOACTIVATE;
            SetWindowPos(hSelectorWindow, HWND_TOPMOST, r->left, r->top, cx, cy, uFlags);
        }
        break;
    case WM_CREATE: {
        const CREATESTRUCT& cs = *reinterpret_cast<CREATESTRUCT *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs.lpCreateParams));
    } break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void AppMenu::createTaskbar() {
    const HWND& parentWin = getAppMain().GetWindowHandle();
    if (!parentWin) {
        Err::Exit(
            "Internal error:", "Taskbar must be created after the main window is created:",
            __FILE__, __LINE__);
    }

    auto iconData = Resource::getStatusIconData();
    hTaskbarIcon_ = CreateIconFromResource(
        const_cast<PBYTE>(iconData.data()), iconData.length(), TRUE, 0x00030000);
    if (!hTaskbarIcon_) {
        Err::Log("Failed to load icon. Fallback to Windows' default application icon.");
        hTaskbarIcon_ = LoadIconA(nullptr, IDI_APPLICATION);
        if (!hTaskbarIcon_) {
            Err::Exit("Icon fallback failed.");
        }
    }

    taskbarIconDesc_.cbSize = sizeof(taskbarIconDesc_);
    taskbarIconDesc_.hWnd = parentWin;
    taskbarIconDesc_.uID = 100;  // TODO: What value should be here?
    taskbarIconDesc_.hIcon = hTaskbarIcon_;
    taskbarIconDesc_.uVersion = NOTIFYICON_VERSION_4;
    taskbarIconDesc_.uCallbackMessage = YOMMD_WM_SHOW_TASKBAR_MENU;
    wcscpy_s(taskbarIconDesc_.szTip, sizeof(taskbarIconDesc_.szTip), L"yoMMD");
    taskbarIconDesc_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;

    Shell_NotifyIconW(NIM_ADD, &taskbarIconDesc_);
}

constexpr AppMenu::Cmd::Kind AppMenu::Cmd::GetCmd(AppMenu::Cmd::UnderlyingType cmd) {
    constexpr UnderlyingType mask = (UnderlyingType(1) << fieldLength_) - 1;
    return Kind(cmd & mask);
}

constexpr AppMenu::Cmd::UnderlyingType AppMenu::Cmd::GetUserData(
    AppMenu::Cmd::UnderlyingType cmd) {
    constexpr UnderlyingType mask = (UnderlyingType(1) << fieldLength_) - 1;
    return ((cmd >> fieldLength_) & mask);
}

constexpr AppMenu::Cmd::UnderlyingType AppMenu::Cmd::Combine(
    AppMenu::Cmd::Kind kind,
    AppMenu::Cmd::UnderlyingType userData) {
    userData <<= fieldLength_;
    return Enum::underlyCast(kind) | userData;
}
