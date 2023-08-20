#define WINVER 0x0605
#include <string>
#include <vector>
#include <string_view>
#include <utility>
#include <type_traits>
#include <memory>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <d3d11.h>
#include <d3d11_2.h>
#include <dcomp.h>
#include <dxgi.h>
#include <wrl.h>
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "glm/glm.hpp"
#include "main.hpp"
#include "viewer.hpp"
#include "util.hpp"
#include "constant.hpp"

template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

namespace {
const void *getRenderTargetView();
const void *getDepthStencilView();
SIZE rectToSize(RECT rect);
std::vector<HMONITOR> getAllMonitorHandles();
std::optional<HMONITOR> getMonitorHandleFromID(int monitorID);
std::optional<RECT> getMonitorWorkareaFromID(int screenID);

template <typename T, typename DeleteFunc, DeleteFunc deleteFunc>
class UniqueHandler {
public:
    UniqueHandler(T handler) : handler_(handler, deleteFunc) {};
    UniqueHandler() : UniqueHandler(nullptr) {};
    T GetRawHandler() {return handler_.get();};
    operator T() {return GetRawHandler();};
    UniqueHandler& operator=(T& handler) {handler_.reset(handler); return *this;};
private:
    std::unique_ptr<std::remove_pointer_t<T>, DeleteFunc> handler_;
};

using UniqueHWND = UniqueHandler<HWND, decltype(&DestroyWindow), &DestroyWindow>;
using UniqueHMENU = UniqueHandler<HMENU, decltype(&DestroyMenu), &DestroyMenu>;
}

class AppMenu {
public:
    AppMenu();
    ~AppMenu();
    void Setup();
    void Terminate();
    void ShowMenu();
public:
    static constexpr UINT YOMMD_WM_TOGGLE_ENABLE_MOUSE = WM_APP;
    static constexpr UINT YOMMD_WM_SHOW_TASKBAR_MENU = WM_APP + 1;
private:
    static DWORD WINAPI showMenu(LPVOID param);
    static LRESULT CALLBACK windowProc(
            HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void createTaskbar();
private:
    class Cmd {
    private:
        template <typename LHS, typename RHS>
        using SelectSmallerSizeType =
            std::conditional<sizeof(LHS) < sizeof(RHS), LHS, RHS>;

        template <typename HD, typename ...TL> struct MinimumSizeType {
            using type = typename SelectSmallerSizeType<
                HD, typename MinimumSizeType<TL...>::type>::type;
        };

        template <typename T> struct MinimumSizeType<T> {
            using type = T;
        };
    public:
        using UnderlyingType = MinimumSizeType<UINT_PTR, UINT, WORD>::type;
        enum class Kind : UnderlyingType {
            None,
            EnableMouse,
            ResetPosition,
            SelectScreen,
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
};

class AppMain {
public:
    AppMain();
    ~AppMain();
    void Setup(const CmdArgs& cmdArgs);
    void UpdateDisplay();
    void Terminate();
    bool IsRunning() const;
    void ChangeScreen(int screenID);
    Routine& GetRoutine();
    const HWND& GetWindowHandle() const;
    sg_context_desc GetSokolContext() const;
    glm::vec2 GetWindowSize() const;
    glm::vec2 GetDrawableSize() const;
    const ID3D11RenderTargetView *GetRenderTargetView() const;
    const ID3D11DepthStencilView *GetDepthStencilView() const;
private:
    static LRESULT CALLBACK windowProc(
            HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void createWindow();
    void createDrawable();
    LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
private:
    static constexpr PCWSTR windowClassName_ = L"yoMMD AppMain";

    bool isRunning_;
    Routine routine_;
    AppMenu menu_;
    HWND hwnd_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11Texture2D> renderTarget_;
    ComPtr<ID3D11RenderTargetView> renderTargetView_;
    ComPtr<ID3D11Device> d3Device_;
    ComPtr<ID3D11DeviceContext> deviceContext_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<IDXGIFactory2> dxFactory_;
    ComPtr<ID3D11Texture2D> depthStencilBuffer_;
    ComPtr<ID3D11DepthStencilView> depthStencilView_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
};

class MsgBox {
public:
    static void Init();
    static void Terminate();
    static void Show(std::string_view);
private:
    static LRESULT CALLBACK windowProc(
        HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void drawContents(HWND hwnd);
private:
    constexpr static PCWSTR className_ = L"yoMMD-messagebox";
    constexpr static int okMenuID_ = 100;
    constexpr static UINT winStyle_ =
        WS_CAPTION | WS_SYSMENU | WS_TABSTOP | WS_DLGFRAME;
    static bool initialized_;
    static bool showingWindow_;
    static HINSTANCE hInstance_;
    static HFONT hfont_;
    static HWND buttonHWND_;
    static std::wstring wmsg_;
};

namespace {
namespace globals {
AppMain appMain;
}
}

AppMain::AppMain() :
    isRunning_(true),
    hwnd_(nullptr)
{}

AppMain::~AppMain() {
    Terminate();
}

void AppMain::Setup(const CmdArgs& cmdArgs) {
    // Tell system not to take it account into size scaling.
    // TODO: Use .manifest file.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    routine_.ParseConfig(cmdArgs);
    createWindow();
    createDrawable();
    menu_.Setup();
    routine_.Init();

    // Every initialization must be finished.  Now let's show window.
    ShowWindow(hwnd_, SW_SHOWNORMAL);
}

void AppMain::UpdateDisplay() {
    routine_.Update();
    routine_.Draw();
    swapChain_->Present(1, 0);
    dcompDevice_->Commit();
}

void AppMain::Terminate() {
    routine_.Terminate();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    UnregisterClassW(windowClassName_, GetModuleHandleW(nullptr));
    menu_.Terminate();
}

bool AppMain::IsRunning() const {
    return isRunning_;
}

void AppMain::ChangeScreen(int screenID) {
    auto r = getMonitorWorkareaFromID(screenID);
    if (!r.has_value()) {
        // It seems the specified monitor is disconnected.  Do nothing.
        return;
    }
    const auto cx = r->right - r->left;
    const auto cy = r->bottom - r->top;
    constexpr UINT uFlags = SWP_SHOWWINDOW | SWP_NOACTIVATE;
    SetWindowPos(hwnd_, HWND_TOP, r->left, r->top, cx, cy, uFlags);  // TODO: HWND_TOPMOST?
}

Routine& AppMain::GetRoutine() {
    return routine_;
}
const HWND& AppMain::GetWindowHandle() const {
    return hwnd_;
}

sg_context_desc AppMain::GetSokolContext() const {
    return sg_context_desc {
        .sample_count = Constant::SampleCount,
        .d3d11 = {
            .device = reinterpret_cast<const void *>(d3Device_.Get()),
            .device_context = reinterpret_cast<const void *>(
                    deviceContext_.Get()),
            .render_target_view_cb = getRenderTargetView,
            .depth_stencil_view_cb = getDepthStencilView,
        }
    };
}

glm::vec2 AppMain::GetWindowSize() const {
    RECT rect;
    if (!GetClientRect(hwnd_, &rect)) {
        Err::Log("Failed to get window rect");
        return glm::vec2(1.0f, 1.0f);  // glm::vec2(0, 0) cause error.
    }
    return glm::vec2(rect.right - rect.left, rect.bottom - rect.top);
}

glm::vec2 AppMain::GetDrawableSize() const {
    D3D11_TEXTURE2D_DESC desc;
    renderTarget_->GetDesc(&desc);
    return glm::vec2(desc.Width, desc.Height);
}

const ID3D11RenderTargetView *AppMain::GetRenderTargetView() const {
    return renderTargetView_.Get();
}

const ID3D11DepthStencilView *AppMain::GetDepthStencilView() const {
    return depthStencilView_.Get();
}

void AppMain::createWindow() {
    constexpr DWORD winStyle = WS_POPUP;
    constexpr DWORD winExStyle =
        WS_EX_NOREDIRECTIONBITMAP |
        WS_EX_NOACTIVATE |
        WS_EX_TOPMOST |
        WS_EX_LAYERED |
        WS_EX_TRANSPARENT;

    const HINSTANCE hInstance = GetModuleHandleW(nullptr);
    const HICON appIcon = LoadIconW(hInstance, L"YOMMD_APPICON_ID");
    if (!appIcon) {
        Err::Log("Failed to load application icon.");
    }

    const Config& config = routine_.GetConfig();
    int targetScreenNumber = 0;  // The main monitor ID should be 0.
    if (config.defaultScreenNumber.has_value()) {
        targetScreenNumber = *config.defaultScreenNumber;
    }

    RECT rect = {};
    if (auto r = getMonitorWorkareaFromID(targetScreenNumber); r.has_value()) {
        rect = *r;
    } else {
        // It seems the specified screen not found.  Use the main screen as
        // fallback.
        r = getMonitorWorkareaFromID(0);
        if (!r.has_value()) {
            Err::Log("Internal error: failed to get display device");
        }
        rect = *r;
    }

    WNDCLASSEXW wc = {};

    wc.cbSize        = sizeof(wc);
    wc.style         = 0;
    wc.lpfnWndProc   = windowProc,
    wc.hInstance     = hInstance;
    wc.lpszClassName = windowClassName_;
    wc.hIcon         = appIcon;
    wc.hIconSm       = appIcon;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        winExStyle, windowClassName_, L"yoMMD", winStyle,
        rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr,
        hInstance, this);

    if (!hwnd_)
        Err::Exit("Failed to create window.");

    // Don't call ShowWindow() here.  Postpone showing window until
    // MMD model setup finished.
}

void AppMain::createDrawable() {
    if (!hwnd_) {
        Err::Exit("Internal error: createDrawable() must be called after createWindow()");
    }
    constexpr auto failif = [](HRESULT hr, auto&& ...errMsg) {
        if (FAILED(hr))
            Err::Exit(std::forward<decltype(errMsg)>(errMsg)...);
    };

    HRESULT hr;

    UINT createFlags = D3D11_CREATE_DEVICE_SINGLETHREADED |
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            nullptr, 0, // Use highest available feature level
            D3D11_SDK_VERSION,
            d3Device_.GetAddressOf(),
            nullptr,
            deviceContext_.GetAddressOf());
    failif(hr, "Failed to create d3d11 device");

    hr = d3Device_.As(&dxgiDevice_);
    failif(hr, "device_.As() failed:", __FILE__, __LINE__);

    hr = CreateDXGIFactory2(0, __uuidof(dxFactory_.Get()),
            reinterpret_cast<void **>(dxFactory_.GetAddressOf()));
    failif(hr, "Failed to create DXGIFactory2");

    glm::vec2 size(GetWindowSize());
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = static_cast<UINT>(size.x);
    swapChainDesc.Height = static_cast<UINT>(size.y);
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = dxFactory_->CreateSwapChainForComposition(
            dxgiDevice_.Get(), &swapChainDesc,
            nullptr, swapChain_.GetAddressOf());
    failif(hr, "Failed to create swap chain.");

    hr = swapChain_->GetBuffer(0, __uuidof(renderTarget_.Get()),
            reinterpret_cast<void **>(renderTarget_.GetAddressOf()));
    failif(hr, "Failed to get buffer from swap chain.");

    hr = d3Device_->CreateRenderTargetView(renderTarget_.Get(), nullptr,
            renderTargetView_.GetAddressOf());
    failif(hr, "Failed to get render target view.");

    D3D11_TEXTURE2D_DESC stencilDesc = {
        .Width = static_cast<UINT>(size.x),
        .Height = static_cast<UINT>(size.y),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_D24_UNORM_S8_UINT,
        .SampleDesc = swapChainDesc.SampleDesc,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_DEPTH_STENCIL,
    };
    hr = d3Device_->CreateTexture2D(
            &stencilDesc, nullptr, depthStencilBuffer_.GetAddressOf());
    failif(hr, "Failed to create depth stencil buffer.");

    D3D11_DEPTH_STENCIL_VIEW_DESC stencilViewDesc = {
        .Format = stencilDesc.Format,
        .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS,
    };
    hr = d3Device_->CreateDepthStencilView(
            reinterpret_cast<ID3D11Resource*>(depthStencilBuffer_.Get()),
            &stencilViewDesc,
            depthStencilView_.GetAddressOf());
    failif(hr, "Failed to create depth stencil view.");

    hr = DCompositionCreateDevice(
       dxgiDevice_.Get(),
       __uuidof(dcompDevice_.Get()),
       reinterpret_cast<void **>(dcompDevice_.GetAddressOf()));
    failif(hr, "Failed to create DirectComposition device.");

    hr = dcompDevice_->CreateTargetForHwnd(
            hwnd_, true, dcompTarget_.GetAddressOf());
    failif(hr, "Failed to DirectComposition render target.");

    hr = dcompDevice_->CreateVisual(dcompVisual_.GetAddressOf());
    failif(hr, "Failed to create DirectComposition visual object.");

    dcompVisual_->SetContent(swapChain_.Get());
    dcompTarget_->SetRoot(dcompVisual_.Get());
}

LRESULT CALLBACK AppMain::windowProc(
        HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    using This_T = AppMain;
    This_T *pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate =
            reinterpret_cast<CREATESTRUCT *>(lParam);
        pThis = static_cast<This_T *>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->hwnd_ = hwnd;
    } else {
        pThis = reinterpret_cast<This_T *>(GetWindowLongPtrW(
                    hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessage(uMsg, wParam, lParam);
    } else {
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

LRESULT AppMain::handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        isRunning_ = false;
        return 0;
    case WM_LBUTTONDOWN:
        routine_.OnMouseDown();
        return 0;
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
            routine_.OnMouseDragged();
        }
        return 0;
    case WM_MOUSEWHEEL:
        {
            const int deltaDeg =
                GET_WHEEL_DELTA_WPARAM(wParam) * WHEEL_DELTA;
            const float delta =
                static_cast<float>(deltaDeg) / 360.0f;
            routine_.OnWheelScrolled(delta);
            return 0;
        }
    case AppMenu::YOMMD_WM_SHOW_TASKBAR_MENU:
        if (const auto msg = LOWORD(lParam);
                !(msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN))
            return 0;
        // Fallthrough
    case WM_RBUTTONDOWN:
        menu_.ShowMenu();
        return 0;
    default:
        return DefWindowProc(hwnd_, uMsg, wParam, lParam);
    }
}

AppMenu::AppMenu() :
    hMenuThread_(nullptr), hTaskbarIcon_(nullptr)
{}

AppMenu::~AppMenu() {
    Terminate();
}

void AppMenu::Setup() {
    WNDCLASSW wc = {};

    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = AppMenu::windowProc,
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = wcMenuName;
    wc.hIcon         = LoadIcon(nullptr, IDI_WINLOGO);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    wc.lpfnWndProc   = DefWindowProcW;
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
    if (GetExitCodeThread(hMenuThread_, &exitCode) &&
            exitCode == STILL_ACTIVE) {
        Info::Log("Thread is still running. Wait finishing.");
        // Right click menu/toolbar menu must not appear so long.
        // We should be able to wait for it is closed.
        WaitForSingleObject(hMenuThread_, INFINITE);
        CloseHandle(hMenuThread_);
    }
}

void AppMenu::ShowMenu() {
    DWORD exitCode;
    if (GetExitCodeThread(hMenuThread_, &exitCode) &&
            exitCode == STILL_ACTIVE) {
        Info::Log("Thread is running");
    }

    hMenuThread_ = CreateThread(
            NULL, 0, AppMenu::showMenu, nullptr, 0, NULL);
}

DWORD WINAPI AppMenu::showMenu(LPVOID param) {
    (void)param;
    constexpr DWORD winStyle = WS_CHILD;
    const HWND& parentWin = globals::appMain.GetWindowHandle();
    UniqueHWND hMenuWindow, hSelectorWindow;

    const LONG parentWinExStyle = GetWindowLongW(parentWin, GWL_EXSTYLE);
    if (parentWinExStyle == 0) {
        Info::Log("Failed to get parent window's style");
    }

    hSelectorWindow = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_NOACTIVATE, wcSelectorName, L"",
            WS_DISABLED | WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
            );
    if (!hSelectorWindow.GetRawHandler()) {
        Err::Log("Failed to create screen selector window.");
        return 1;
    }
    SetLayeredWindowAttributes(hSelectorWindow, RGB(0, 0, 0), 127, LWA_ALPHA);
    BOOL fDisable = TRUE;
    DwmSetWindowAttribute(hSelectorWindow,
            DWMWA_TRANSITIONS_FORCEDISABLED, &fDisable, sizeof(fDisable));

    hMenuWindow = CreateWindowExW(
            0, wcMenuName, L"", winStyle, 0, 0, 0, 0, parentWin, nullptr,
            GetModuleHandleW(nullptr), hSelectorWindow
            );
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
    HMONITOR curMonitorHandle =
        MonitorFromWindow(globals::appMain.GetWindowHandle(), MONITOR_DEFAULTTONULL);
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
    AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::EnableMouse), L"&Enable Mouse");
    AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::ResetPosition), L"&Reset Position");
    AppendMenuW(hmenu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(hmenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hScreensMenu.GetRawHandler()), L"&Select screen");
    AppendMenuW(hmenu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(hmenu, MF_STRING, Enum::underlyCast(Cmd::Quit), L"&Quit");

    if (parentWinExStyle == 0) {
        EnableMenuItem(hmenu, Enum::underlyCast(Cmd::EnableMouse), MF_DISABLED);
    } else if (parentWinExStyle & WS_EX_TRANSPARENT) {
        CheckMenuItem(hmenu, Enum::underlyCast(Cmd::EnableMouse), MF_UNCHECKED);
    } else {
        CheckMenuItem(hmenu, Enum::underlyCast(Cmd::EnableMouse), MF_CHECKED);
    }

    if (monitorHandles.size() <= 1)
        EnableMenuItem(hmenu, reinterpret_cast<UINT_PTR>(hScreensMenu.GetRawHandler()), MF_DISABLED);

    constexpr UINT menuFlags = TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD;

    SetForegroundWindow(hMenuWindow);
    const auto op = TrackPopupMenuEx(
            hmenu, menuFlags, point.x, point.y, hMenuWindow, nullptr);

    switch (Cmd::GetCmd(op)) {
    case Cmd::EnableMouse:
        if (parentWinExStyle != 0) {
            SetWindowLongW(parentWin, GWL_EXSTYLE,
                    parentWinExStyle ^ WS_EX_TRANSPARENT);
        }
        break;
    case Cmd::ResetPosition:
        globals::appMain.GetRoutine().ResetModelPosition();
        break;
    case Cmd::SelectScreen:
        globals::appMain.ChangeScreen(Cmd::GetUserData(op));
        break;
    case Cmd::Quit:
        SendMessageW(parentWin, WM_DESTROY, 0, 0);
        break;
    case Cmd::None:
        // Canceled. Do nothing.
        break;
    case Cmd::MenuCount:
        Err::Log("Internal error: Command::MenuCount is used");
        break;
    }

    return 0;
}

LRESULT CALLBACK AppMenu::windowProc(
        HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
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
            SetWindowPos(
                    hSelectorWindow, HWND_TOPMOST, r->left, r->top, cx, cy, uFlags);
        }
        break;
    case WM_CREATE:
        {
            const CREATESTRUCT& cs = *reinterpret_cast<CREATESTRUCT *>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(cs.lpCreateParams));
        }
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void AppMenu::createTaskbar() {
    const HWND& parentWin = globals::appMain.GetWindowHandle();
    if (!parentWin) {
        Err::Exit(
                "Internal error:",
                "Taskbar must be created after the main window is created:",
                __FILE__, __LINE__
                );
    }

    auto iconData = Resource::getStatusIconData();
    hTaskbarIcon_ = CreateIconFromResource(
            const_cast<PBYTE>(iconData.data()),
            iconData.length(),
            TRUE,
            0x00030000);
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
    wcscpy_s(taskbarIconDesc_.szTip,
            sizeof(taskbarIconDesc_.szTip), L"yoMMD");
    taskbarIconDesc_.uFlags =
        NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;

    Shell_NotifyIconW(NIM_ADD, &taskbarIconDesc_);
}

constexpr AppMenu::Cmd::Kind AppMenu::Cmd::GetCmd(AppMenu::Cmd::UnderlyingType cmd) {
    constexpr UnderlyingType mask = (UnderlyingType(1) << fieldLength_) - 1;
    return Kind(cmd & mask);
}

constexpr AppMenu::Cmd::UnderlyingType AppMenu::Cmd::GetUserData(AppMenu::Cmd::UnderlyingType cmd) {
    constexpr UnderlyingType mask = (UnderlyingType(1) << fieldLength_) - 1;
    return ((cmd >> fieldLength_) & mask);
}

constexpr AppMenu::Cmd::UnderlyingType AppMenu::Cmd::Combine(
        AppMenu::Cmd::Kind kind, AppMenu::Cmd::UnderlyingType userData) {
    userData <<= fieldLength_;
    return Enum::underlyCast(kind) | userData;
}

bool MsgBox::initialized_ = false;
bool MsgBox::showingWindow_ = false;
HINSTANCE MsgBox::hInstance_ = nullptr;
HFONT MsgBox::hfont_ = nullptr;
HWND MsgBox::buttonHWND_ = nullptr;
std::wstring MsgBox::wmsg_{};

void MsgBox::Init() {
    hInstance_ = GetModuleHandleW(nullptr);
    hfont_ = static_cast<HFONT>(GetStockObject(OEM_FIXED_FONT));

    WNDCLASSW wc;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = windowProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance_;
    wc.hIcon         = LoadIcon(nullptr, IDI_WARNING);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszMenuName  = nullptr;
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

    const int size = MultiByteToWideChar(
            CP_UTF8, MB_COMPOSITE, msg.data(), -1, nullptr, 0);
    wmsg_.resize(size-1, '\0');
    MultiByteToWideChar(
            CP_UTF8, MB_COMPOSITE, msg.data(), -1, wmsg_.data(), size);

    const HWND hwnd = CreateWindowW(
            className_, L"yoMMD Error",
            winStyle_,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            nullptr, nullptr,
            hInstance_, nullptr);

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

LRESULT CALLBACK MsgBox::windowProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        {
            constexpr UINT winStyle =
                WS_CHILD | WS_VISIBLE |
                BS_CENTER | BS_VCENTER | BS_DEFPUSHBUTTON;

            buttonHWND_ = CreateWindowW(L"BUTTON", L"OK",
                    winStyle,
                    0, 0, 60, 25,
                    hwnd, reinterpret_cast<HMENU>(okMenuID_),
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
    hPrevFont =
        static_cast<HFONT>(SelectObject(hdc, hfont_));

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
    MoveWindow(hwnd,
            winRect.left, winRect.top,
            winSize.cx, winSize.cy,
            FALSE);

    // Adjust button position.
    buttonPos = rectToSize(contentRect);
    buttonPos.cx -= textMerginX + buttonSize.cx;
    buttonPos.cy -= buttonMerginY + buttonSize.cy;
    MoveWindow(buttonHWND_,
            buttonPos.cx, buttonPos.cy,
            buttonSize.cx, buttonSize.cy,
            FALSE);

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

namespace Context {
sg_context_desc getSokolContext() {
    return globals::appMain.GetSokolContext();
}
glm::vec2 getWindowSize() {
    return globals::appMain.GetWindowSize();
}
glm::vec2 getDrawableSize() {
    return globals::appMain.GetDrawableSize();
}
glm::vec2 getMousePosition() {
    POINT pos;
    if (!GetCursorPos(&pos))
        return glm::vec2();
    int sizeY = GetSystemMetrics(SM_CYSCREEN);
    return glm::vec2(pos.x, sizeY - pos.y);  // Make origin bottom-left.
}
}

namespace Dialog {
void messageBox(std::string_view msg) {
    MsgBox::Show(msg);
}
}

namespace {
const void *getRenderTargetView() {
    return reinterpret_cast<const void *>(
            globals::appMain.GetRenderTargetView());
}
const void *getDepthStencilView() {
    return reinterpret_cast<const void *>(
            globals::appMain.GetDepthStencilView());
}
SIZE rectToSize(RECT rect) {
    return {rect.right - rect.left, rect.bottom - rect.top};
}
std::vector<HMONITOR> getAllMonitorHandles() {
    static const MONITORENUMPROC proc = [](
            HMONITOR hMonitor,
            HDC hdc,
            LPRECT rect,
            LPARAM param) -> BOOL {
        (void)hdc, (void)rect;
        std::vector<HMONITOR>& handles =
            *reinterpret_cast<std::vector<HMONITOR>*>(param);
        handles.push_back(hMonitor);
        return TRUE;
    };
    std::vector<HMONITOR> handles;
    EnumDisplayMonitors(nullptr, nullptr, proc, reinterpret_cast<LPARAM>(&handles));
    return handles;
}
std::optional<HMONITOR> getMonitorHandleFromID(int monitorID) {
    struct Data {
        const int monitorID;
        int curMonitorID;
        std::optional<HMONITOR> handle;
    };
    static const MONITORENUMPROC proc = [](
            HMONITOR hMonitor,
            HDC hdc,
            LPRECT rect,
            LPARAM param) -> BOOL {
        (void)hdc, (void)rect;
        Data *data = reinterpret_cast<Data *>(param);
        if (data->curMonitorID == data->monitorID) {
            data->handle = hMonitor;
            return FALSE;
        }
        data->curMonitorID++;
        return TRUE;
    };
    Data data = {.monitorID = monitorID, .curMonitorID = 0, .handle = std::nullopt};
    EnumDisplayMonitors(nullptr, nullptr, proc, reinterpret_cast<LPARAM>(&data));
    return data.handle;
}
std::optional<RECT> getMonitorWorkareaFromID(int monitorID) {
    const auto handle = getMonitorHandleFromID(monitorID);
    if (!handle.has_value())
        return std::nullopt;

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(*handle, &info);
    return info.rcWork;
}

}  // namespace

int WINAPI wWinMain(
        HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)pCmdLine;
    (void)nCmdShow;

    int argc = 0;
    LPWSTR cmdline = GetCommandLineW();
    LPWSTR *argv = CommandLineToArgvW(cmdline, &argc);

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(String::wideToMulti<char>(argv[i]));
    }

    const auto cmdArgs = CmdArgs::Parse(args);

    args.clear();

    MsgBox::Init();
    globals::appMain.Setup(cmdArgs);

    MSG msg = {};
    constexpr double millSecPerFrame = 1000.0 / Constant::FPS;
    uint64_t timeLastFrame = stm_now();
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!globals::appMain.IsRunning())
            break;

        globals::appMain.UpdateDisplay();

        const double elapsedMillSec = stm_ms(stm_since(timeLastFrame));
        const auto shouldSleepFor = millSecPerFrame - elapsedMillSec;
        timeLastFrame = stm_now();

        if (shouldSleepFor > 0 &&
                static_cast<DWORD>(shouldSleepFor) > 0) {
            Sleep(static_cast<DWORD>(shouldSleepFor));
        }
    }
    globals::appMain.Terminate();
    MsgBox::Terminate();

    return 0;
}
