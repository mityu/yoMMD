#include "main.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "../constant.hpp"
#include "../keyboard.hpp"
#include "../platform_api.hpp"
#include "glm/vec2.hpp"  // IWYU pragma: keep; silence clangd.
#include "menu.hpp"
#include "msgbox.hpp"
#include "sokol_time.h"

AppMain::AppMain() :
    isRunning_(true), sampleCount_(Constant::PreferredSampleCount), hwnd_(nullptr) {}

AppMain::~AppMain() {
    Terminate();
}

void AppMain::Setup(const CmdArgs& cmdArgs) {
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
    const auto r = getMonitorWorkareaFromID(screenID);
    if (!r.has_value()) {
        // It seems the specified monitor is disconnected.  Do nothing.
        return;
    }
    const auto cx = r->right - r->left;
    const auto cy = r->bottom - r->top;
    constexpr UINT uFlags = SWP_SHOWWINDOW | SWP_NOACTIVATE;
    SetWindowPos(hwnd_, HWND_TOPMOST, r->left, r->top, cx, cy, uFlags);
}

Routine& AppMain::GetRoutine() {
    return routine_;
}
const HWND& AppMain::GetWindowHandle() const {
    return hwnd_;
}

sg_environment AppMain::GetSokolEnvironment() const {
    return sg_environment{
        .defaults =
            {
                .color_format = SG_PIXELFORMAT_BGRA8,
                .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                .sample_count = sampleCount_,
            },
        .d3d11 =
            {
                .device = reinterpret_cast<const void *>(d3Device_.Get()),
                .device_context = reinterpret_cast<const void *>(deviceContext_.Get()),
            },
    };
}

sg_swapchain AppMain::GetSokolSwapchain() const {
    const auto size{Context::getWindowSize()};
    sg_d3d11_swapchain d3d11 = {.depth_stencil_view = depthStencilView_.Get()};
    if (sampleCount_ > 1) {
        d3d11.render_view = msaaRenderTargetView_.Get();
        d3d11.resolve_view = renderTargetView_.Get();
    } else {
        d3d11.render_view = renderTargetView_.Get();
        d3d11.resolve_view = nullptr;
    }
    return sg_swapchain{
        .width = static_cast<int>(size.x),
        .height = static_cast<int>(size.y),
        .sample_count = sampleCount_,
        .color_format = SG_PIXELFORMAT_BGRA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .d3d11 = d3d11,
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

int AppMain::GetSampleCount() const {
    return sampleCount_;
}
bool AppMain::IsMenuOpened() const {
    return menu_.IsMenuOpened();
}

void AppMain::createWindow() {
    constexpr DWORD winStyle = WS_POPUP;
    constexpr DWORD winExStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TOPMOST |
                                 WS_EX_LAYERED | WS_EX_TRANSPARENT;

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

    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = windowProc, wc.hInstance = hInstance;
    wc.lpszClassName = windowClassName_;
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        winExStyle, windowClassName_, L"yoMMD", winStyle, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, this);

    if (!hwnd_)
        Err::Exit("Failed to create window.");

    // Don't call ShowWindow() here.  Postpone showing window until
    // MMD model setup finished.
}

void AppMain::createDrawable() {
    if (!hwnd_) {
        Err::Exit("Internal error: createDrawable() must be called after createWindow()");
    }
    constexpr auto failif = [](HRESULT hr, auto&&...errMsg) {
        if (FAILED(hr))
            Err::Exit(std::forward<decltype(errMsg)>(errMsg)...);
    };

    HRESULT hr;

    // Direct3D 11 setups.
    UINT createFlags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, nullptr,
        0,  // Use highest available feature level
        D3D11_SDK_VERSION, d3Device_.GetAddressOf(), nullptr, deviceContext_.GetAddressOf());
    failif(hr, "Failed to create d3d11 device");

    hr = d3Device_.As(&dxgiDevice_);
    failif(hr, "device_.As() failed:", __FILE__, __LINE__);

    hr = CreateDXGIFactory2(
        0, __uuidof(dxFactory_.Get()), reinterpret_cast<void **>(dxFactory_.GetAddressOf()));
    failif(hr, "Failed to create DXGIFactory2");

    const glm::vec2 size(GetWindowSize());
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
        dxgiDevice_.Get(), &swapChainDesc, nullptr, swapChain_.GetAddressOf());
    failif(hr, "Failed to create swap chain.");

    hr = swapChain_->GetBuffer(
        0, __uuidof(renderTarget_.Get()),
        reinterpret_cast<void **>(renderTarget_.GetAddressOf()));
    failif(hr, "Failed to get buffer from swap chain.");

    hr = d3Device_->CreateRenderTargetView(
        renderTarget_.Get(), nullptr, renderTargetView_.GetAddressOf());
    failif(hr, "Failed to get render target view.");

    sampleCount_ = determineSampleCount();

    const D3D11_TEXTURE2D_DESC msaaTextureDesc = {
        .Width = static_cast<UINT>(size.x),
        .Height = static_cast<UINT>(size.y),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc =
            {
                .Count = static_cast<UINT>(Constant::PreferredSampleCount),
                .Quality = D3D11_STANDARD_MULTISAMPLE_PATTERN,
            },
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET,
    };

    if (sampleCount_ > 1) {
        hr = d3Device_->CreateTexture2D(
            &msaaTextureDesc, nullptr, msaaRenderTarget_.GetAddressOf());
        failif(hr, "Failed to create msaa render target.");
        hr = d3Device_->CreateRenderTargetView(
            msaaRenderTarget_.Get(), nullptr, msaaRenderTargetView_.GetAddressOf());
        failif(hr, "Failed to get msaa render target view.");
    }

    const D3D11_TEXTURE2D_DESC stencilDesc = {
        .Width = static_cast<UINT>(size.x),
        .Height = static_cast<UINT>(size.y),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_D24_UNORM_S8_UINT,
        .SampleDesc = sampleCount_ > 1 ? msaaTextureDesc.SampleDesc : swapChainDesc.SampleDesc,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_DEPTH_STENCIL,
    };
    hr = d3Device_->CreateTexture2D(&stencilDesc, nullptr, depthStencilBuffer_.GetAddressOf());
    failif(hr, "Failed to create depth stencil buffer.");

    const D3D11_DEPTH_STENCIL_VIEW_DESC stencilViewDesc = {
        .Format = stencilDesc.Format,
        .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS,
    };
    hr = d3Device_->CreateDepthStencilView(
        reinterpret_cast<ID3D11Resource *>(depthStencilBuffer_.Get()), &stencilViewDesc,
        depthStencilView_.GetAddressOf());
    failif(hr, "Failed to create depth stencil view.");

    // DirectComposition setups.
    hr = DCompositionCreateDevice(
        dxgiDevice_.Get(), __uuidof(dcompDevice_.Get()),
        reinterpret_cast<void **>(dcompDevice_.GetAddressOf()));
    failif(hr, "Failed to create DirectComposition device.");

    hr = dcompDevice_->CreateTargetForHwnd(hwnd_, true, dcompTarget_.GetAddressOf());
    failif(hr, "Failed to DirectComposition render target.");

    hr = dcompDevice_->CreateVisual(dcompVisual_.GetAddressOf());
    failif(hr, "Failed to create DirectComposition visual object.");

    dcompVisual_->SetContent(swapChain_.Get());
    dcompTarget_->SetRoot(dcompVisual_.Get());
}

// Check the state of multisampling support.
// https://learn.microsoft.com/ja-jp/windows/uwp/gaming/multisampling--multi-sample-anti-aliasing--in-windows-store-apps
int AppMain::determineSampleCount() const {
    if (!d3Device_.Get()) {
        Err::Exit(
            "Internal error: checkMultisamplingSupported():",
            "D3D11 device is not initialized.");
    }

    HRESULT hr;

    // Check if the buffer format DXGI_FORMAT_B8G8R8A8_UNORM supports
    // multisampling.
    UINT formatSupport = 0;
    hr = d3Device_->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &formatSupport);

    if (FAILED(hr)) {
        Err::Exit("CheckFormatSupport() failed.");
    } else if (!((formatSupport & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE) &&
                 (formatSupport & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET))) {
        // DXGI_FORMAT_B8G8R8A8_UNORM doesn't support multisampling on the
        // curent device.
        return 1;
    }

    // TODO: Fallback to smaller sample count when the given sample count is
    // not supported.
    UINT numQualityFlags = 0;
    hr = d3Device_->CheckMultisampleQualityLevels(
        DXGI_FORMAT_B8G8R8A8_UNORM, Constant::PreferredSampleCount, &numQualityFlags);

    if (FAILED(hr)) {
        Err::Exit("CheckMultisampleQualityLevels() failed.");
    } else if (numQualityFlags <= 0) {
        // Multisampling with the given sample count is not supported.
        return 1;
    }

    return Constant::PreferredSampleCount;
}

LRESULT CALLBACK AppMain::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    using This_T = AppMain;
    This_T *pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
        pThis = static_cast<This_T *>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->hwnd_ = hwnd;
    } else {
        pThis = reinterpret_cast<This_T *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
    case WM_KEYDOWN:  // fallthrough
    case WM_KEYUP: {
        std::optional<Keycode> key;
        switch (wParam) {
        case VK_SHIFT:
            key = Keycode::Shift;
            break;
        }
        if (!key.has_value())
            break;

        if (uMsg == WM_KEYDOWN)
            Keyboard::OnKeyDown(*key);
        else
            Keyboard::OnKeyUp(*key);
        return 0;
    }
    case WM_LBUTTONDOWN:
        routine_.OnGestureBegin();
        return 0;
    case WM_LBUTTONUP:
        routine_.OnGestureEnd();
        return 0;
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
            routine_.OnMouseDragged();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        const int deltaDeg = GET_WHEEL_DELTA_WPARAM(wParam) * WHEEL_DELTA;
        const float delta = static_cast<float>(deltaDeg) / 360.0f;
        routine_.OnWheelScrolled(delta);
        return 0;
    }
    case AppMenu::YOMMD_WM_SHOW_TASKBAR_MENU:
        if (const auto msg = LOWORD(lParam); !(msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN))
            return 0;
        // Fallthrough
    case WM_RBUTTONDOWN:
        routine_.OnGestureEnd();
        menu_.ShowMenu();
        return 0;
    }
    return DefWindowProcW(hwnd_, uMsg, wParam, lParam);
}

namespace Context {
sg_environment getSokolEnvironment() {
    return getAppMain().GetSokolEnvironment();
}
sg_swapchain getSokolSwapchain() {
    return getAppMain().GetSokolSwapchain();
}
glm::vec2 getWindowSize() {
    return getAppMain().GetWindowSize();
}
glm::vec2 getDrawableSize() {
    return getAppMain().GetDrawableSize();
}
glm::vec2 getMousePosition() {
    // The origin of device positions given by WinAPI is top-left of
    // screens/windows.
    POINT pos;
    if (!GetCursorPos(&pos))
        return glm::vec2();

    const HMONITOR curMonitorHandle =
        MonitorFromWindow(getAppMain().GetWindowHandle(), MONITOR_DEFAULTTONULL);
    if (!curMonitorHandle)
        Err::Exit("Internal error: failed to get current monitor handle.");

    MONITORINFO monitorInfo = {.cbSize = sizeof(monitorInfo)};
    GetMonitorInfoW(curMonitorHandle, &monitorInfo);

    RECT wr = {};
    GetClientRect(getAppMain().GetWindowHandle(), &wr);

    // Get the mouse position that relative to the main window.  Note that
    // origin is top-left of window.
    pos.x = pos.x - monitorInfo.rcMonitor.left - wr.left;
    pos.y = pos.y - monitorInfo.rcMonitor.top - wr.top;

    const auto winHeight = wr.bottom - wr.top;
    return glm::vec2(pos.x, winHeight - pos.y);  // Make origin bottom-left.
}
int getSampleCount() {
    return getAppMain().GetSampleCount();
}
bool shouldEmphasizeModel() {
    return getAppMain().IsMenuOpened();
}
}  // namespace Context

namespace Dialog {
void messageBox(std::string_view msg) {
    MsgBox::Show(msg);
}
}  // namespace Dialog

std::vector<HMONITOR> getAllMonitorHandles() {
    static const MONITORENUMPROC proc = [](HMONITOR hMonitor, HDC hdc, LPRECT rect,
                                           LPARAM param) -> BOOL {
        (void)hdc, (void)rect;
        std::vector<HMONITOR>& handles = *reinterpret_cast<std::vector<HMONITOR> *>(param);
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
    static const MONITORENUMPROC proc = [](HMONITOR hMonitor, HDC hdc, LPRECT rect,
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

AppMain& getAppMain() {
    static AppMain appMain{};
    return appMain;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)pCmdLine;
    (void)nCmdShow;

    int argc = 0;
    const LPWSTR cmdline = GetCommandLineW();
    const LPWSTR *argv = CommandLineToArgvW(cmdline, &argc);

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(String::wideToMulti<char>(argv[i]));
    }

    const auto cmdArgs = CmdArgs::Parse(args);

    args.clear();

    MsgBox::Init();
    getAppMain().Setup(cmdArgs);

    MSG msg = {};
    constexpr double millSecPerFrame = 1000.0 / Constant::FPS;
    uint64_t timeLastFrame = stm_now();
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!getAppMain().IsRunning())
            break;

        getAppMain().UpdateDisplay();

        const double elapsedMillSec = stm_ms(stm_since(timeLastFrame));
        const auto shouldSleepFor = millSecPerFrame - elapsedMillSec;
        timeLastFrame = stm_now();

        if (shouldSleepFor > 0 && static_cast<DWORD>(shouldSleepFor) > 0) {
            Sleep(static_cast<DWORD>(shouldSleepFor));
        }
    }
    getAppMain().Terminate();
    MsgBox::Terminate();

    return 0;
}
