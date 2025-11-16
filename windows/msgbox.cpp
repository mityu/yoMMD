#include "msgbox.hpp"

bool MsgBox::initialized_ = false;
bool MsgBox::showingWindow_ = false;
HINSTANCE MsgBox::hInstance_ = nullptr;
HFONT MsgBox::hfont_ = nullptr;
HWND MsgBox::buttonHWND_ = nullptr;
std::wstring MsgBox::wmsg_{};

namespace {
SIZE rectToSize(RECT rect);
}

void MsgBox::Init() {
    hInstance_ = GetModuleHandleW(nullptr);
    hfont_ = static_cast<HFONT>(GetStockObject(OEM_FIXED_FONT));

    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance_;
    wc.hIcon = LoadIcon(nullptr, IDI_WARNING);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = className_;

    RegisterClassW(&wc);

    initialized_ = true;
}

void MsgBox::Terminate() {
    UnregisterClassW(className_, hInstance_);
    wmsg_.clear();
    hInstance_ = nullptr;
    initialized_ = false;
}

void MsgBox::Show(std::string_view msg) {
    if (!initialized_ || showingWindow_)
        return;

    const int size = MultiByteToWideChar(CP_UTF8, MB_COMPOSITE, msg.data(), -1, nullptr, 0);
    wmsg_.resize(size - 1, '\0');
    MultiByteToWideChar(CP_UTF8, MB_COMPOSITE, msg.data(), -1, wmsg_.data(), size);

    const HWND hwnd = CreateWindowW(
        className_, L"yoMMD Error", winStyle_, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, nullptr, nullptr, hInstance_, nullptr);

    if (!hwnd)
        return;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    SetForegroundWindow(hwnd);

    showingWindow_ = true;

    MSG procMsg;
    while (showingWindow_ && GetMessageW(&procMsg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &procMsg)) {
            TranslateMessage(&procMsg);
            DispatchMessage(&procMsg);
        }
    }
}

LRESULT CALLBACK MsgBox::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        constexpr UINT winStyle =
            WS_CHILD | WS_VISIBLE | BS_CENTER | BS_VCENTER | BS_DEFPUSHBUTTON;

        buttonHWND_ = CreateWindowW(
            L"BUTTON", L"OK", winStyle, 0, 0, 60, 25, hwnd, reinterpret_cast<HMENU>(okMenuID_),
            hInstance_, nullptr);
        // FIXME: Button is not selected in default when showing two
        // error dialog continuously.
        SetForegroundWindow(buttonHWND_);
        return 0;
    }
    case WM_PAINT:
        drawContents(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == okMenuID_) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        DestroyWindow(buttonHWND_);
        wmsg_.clear();
        showingWindow_ = false;
        buttonHWND_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void MsgBox::drawContents(HWND hwnd) {
    constexpr int textMerginX = 50;
    constexpr int textMerginY = 25;
    constexpr int buttonMerginY = 5;

    HDC hdc;
    HFONT hPrevFont;
    PAINTSTRUCT ps = {};

    RECT bounds = {};

    RECT buttonRect = {};
    SIZE buttonPos = {};
    SIZE buttonSize = {};

    SIZE screenSize = {};

    GetClientRect(buttonHWND_, &buttonRect);
    buttonSize = rectToSize(buttonRect);

    hdc = BeginPaint(hwnd, &ps);

    // Change font.
    hPrevFont = static_cast<HFONT>(SelectObject(hdc, hfont_));

    DrawTextW(hdc, wmsg_.data(), -1, &bounds, DT_CALCRECT);

    // Calculate window content area.
    RECT contentRect(bounds);
    contentRect.right += textMerginX * 2;
    contentRect.bottom += textMerginY * 2 + buttonSize.cy + buttonMerginY;

    // Adjust window position.
    // TODO: Check if window size over screen size.  If window is bigger than
    // screen, we need to make font and then window make small.
    RECT winRect(contentRect);
    screenSize.cx = GetSystemMetrics(SM_CXSCREEN);
    screenSize.cy = GetSystemMetrics(SM_CYSCREEN);
    AdjustWindowRect(&winRect, winStyle_, TRUE);
    SIZE winSize(rectToSize(winRect));
    winRect.left = (screenSize.cx - winSize.cx) / 2;
    winRect.top = (screenSize.cy - winSize.cy) / 2;
    MoveWindow(hwnd, winRect.left, winRect.top, winSize.cx, winSize.cy, FALSE);

    // Adjust button position.
    buttonPos = rectToSize(contentRect);
    buttonPos.cx -= textMerginX + buttonSize.cx;
    buttonPos.cy -= buttonMerginY + buttonSize.cy;
    MoveWindow(buttonHWND_, buttonPos.cx, buttonPos.cy, buttonSize.cx, buttonSize.cy, FALSE);

    // Adjust text position.
    bounds.right = bounds.right - bounds.left + textMerginX * 2;
    bounds.bottom = bounds.bottom - bounds.top + textMerginY * 2;
    bounds.left = textMerginX;
    bounds.top = textMerginY;
    DrawTextW(hdc, wmsg_.data(), -1, &bounds, DT_LEFT);

    // Restore font.
    SelectObject(hdc, hPrevFont);
    EndPaint(hwnd, &ps);
}

namespace {
SIZE rectToSize(RECT rect) {
    return {rect.right - rect.left, rect.bottom - rect.top};
}
}  // namespace
