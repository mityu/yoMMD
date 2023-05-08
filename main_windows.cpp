#include <string>
#include <vector>
#include <string_view>
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "glm/glm.hpp"
#include "yommd.hpp"

namespace {
const void *getRenderTargetView();
const void *getDepthStencilView();
}

class AppMain {
public:
    AppMain();
    ~AppMain();
    void Setup(const CmdArgs& cmdArgs);
    void UpdateDisplay();
    void Terminate();
    bool IsRunning() const;
    sg_context_desc GetSokolContext() const;
    glm::vec2 GetWindowSize() const;
    glm::vec2 GetDrawableSize() const;
    const ID3D11RenderTargetView *GetRenderTargetView() const;
    const ID3D11DepthStencilView *GetDepthStencilView() const;
private:
    static LRESULT CALLBACK windowProc(
            HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI showMenu(LPVOID param);

    void createWindow();
    void createDrawable();
    void destroyDrawable();
    void createStatusIcon();
    LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    template <typename T> void safeRelease(T **obj);
private:
    static constexpr PCWSTR windowClassName_ = L"yoMMD AppMain";
    static constexpr UINT YOMMD_WM_TOGGLE_ENABLE_MOUSE = WM_APP;

    bool isRunning_;
    Routine routine_;
    HWND hwnd_;
    DXGI_SWAP_CHAIN_DESC swapChainDesc_;
    IDXGISwapChain *swapChain_;
    ID3D11Texture2D *renderTarget_;
    ID3D11RenderTargetView *renderTargetView_;
    ID3D11Device *device_;
    ID3D11DeviceContext *deviceContext_;
    ID3D11Texture2D *depthStencilBuffer_;
    ID3D11DepthStencilView *depthStencilView_;

    HANDLE hMenuThread_;
    HICON hStatusIcon_;
};

namespace {
namespace globals {
AppMain appMain;
}
}

AppMain::AppMain() :
    isRunning_(true),
    hwnd_(nullptr),
    swapChainDesc_({}), swapChain_(nullptr),
    renderTarget_(nullptr), renderTargetView_(nullptr),
    device_(nullptr), deviceContext_(nullptr),
    depthStencilBuffer_(nullptr), depthStencilView_(nullptr),
    hMenuThread_(nullptr), hStatusIcon_(nullptr)
{}

AppMain::~AppMain() {
    Terminate();
}

void AppMain::Setup(const CmdArgs& cmdArgs) {
    createWindow();
    createDrawable();
    createStatusIcon();
    routine_.Init(cmdArgs);

    // FIXME: Remove title bar here, because doing this in createWindow()
    // cause crash. (Somehow GetWindowSize() is called but it returns
    // glm::vec2{0, 0} and it misses assersion done in glm library.)
    SetWindowLongW(hwnd_, GWL_STYLE, WS_POPUP);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED ) ;
    ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
}

void AppMain::UpdateDisplay() {
    routine_.Update();
    routine_.Draw();
    swapChain_->Present(1, 0);
}

void AppMain::Terminate() {
    routine_.Terminate();
    destroyDrawable();
    safeRelease(&swapChain_);
    safeRelease(&deviceContext_);
    safeRelease(&device_);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    UnregisterClassW(windowClassName_, GetModuleHandleW(nullptr));

    if (hStatusIcon_) {
        DestroyIcon(hStatusIcon_);
        hStatusIcon_ = nullptr;
    }

    DWORD exitCode;
    if (GetExitCodeThread(hMenuThread_, &exitCode) &&
            exitCode == STILL_ACTIVE) {
        Info::Log("Thread is still running. Wait finishing.");
        WaitForSingleObject(hMenuThread_, INFINITE);
        CloseHandle(hMenuThread_);
    }
}

bool AppMain::IsRunning() const {
    return isRunning_;
}

sg_context_desc AppMain::GetSokolContext() const {
    return sg_context_desc {
        .sample_count = Constant::SampleCount,
        .d3d11 = {
            .device = reinterpret_cast<const void *>(device_),
            .device_context = reinterpret_cast<const void *>(deviceContext_),
            .render_target_view_cb = getRenderTargetView,
            .depth_stencil_view_cb = getDepthStencilView,
        }
    };
}

glm::vec2 AppMain::GetWindowSize() const {
    RECT rect;
    if (!GetClientRect(hwnd_, &rect)) {
        Err::Log("Failed to get window rect");
        return glm::vec2();
    }
    return glm::vec2(rect.right - rect.left, rect.bottom - rect.top);
}

glm::vec2 AppMain::GetDrawableSize() const {
    D3D11_TEXTURE2D_DESC desc;
    renderTarget_->GetDesc(&desc);
    return glm::vec2(desc.Width, desc.Height);
}

const ID3D11RenderTargetView *AppMain::GetRenderTargetView() const {
    return renderTargetView_;
}

const ID3D11DepthStencilView *AppMain::GetDepthStencilView() const {
    return depthStencilView_;
}

void AppMain::createWindow() {
    DWORD winStyle = WS_OVERLAPPEDWINDOW | WS_MAXIMIZE;
    DWORD winExStyle = WS_EX_LAYERED | /* WS_EX_TRANSPARENT |*/ WS_EX_TOPMOST;
    LONG width = CW_USEDEFAULT;
    LONG height = CW_USEDEFAULT;

    WNDCLASSW wc = {};

    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = windowProc,
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = windowClassName_;
    wc.hIcon         = LoadIcon(nullptr, IDI_WINLOGO);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        winExStyle, windowClassName_, L"yoMMD", winStyle,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        nullptr, nullptr,
        GetModuleHandleW(nullptr), this);

    if (!hwnd_)
        Err::Exit("Failed to create window.");


    // NOTE: Don't call ShowWindow() here.  It's called later.

    SetWindowLongW(hwnd_, GWL_EXSTYLE, winExStyle);
    SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 255, LWA_ALPHA | LWA_COLORKEY);
}

void AppMain::createDrawable() {
    if (!hwnd_) {
        Err::Exit("Internal error: createDrawable() must be called after createWindow()");
    }

    glm::vec2 size(GetWindowSize());
    Info::Log("size:", size.x, size.y);

    swapChainDesc_ = DXGI_SWAP_CHAIN_DESC {
        .BufferDesc = {
            .Width = static_cast<UINT>(size.x),
            .Height = static_cast<UINT>(size.y),
            .RefreshRate = {
                .Numerator = static_cast<UINT>(Constant::FPS),
                .Denominator = 1,
            },
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        },
        .SampleDesc = {
            .Count = static_cast<UINT>(Constant::SampleCount),
            .Quality = static_cast<UINT>(D3D11_STANDARD_MULTISAMPLE_PATTERN),
        },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 1,
        .OutputWindow = hwnd_,
        .Windowed = true,
        .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
    };
    UINT createFlags = D3D11_CREATE_DEVICE_SINGLETHREADED |
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    #ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                    // pAdapter (use default)
        D3D_DRIVER_TYPE_HARDWARE,   // DriverType
        nullptr,                    // Software
        createFlags,                // Flags
        nullptr,                    // pFeatureLevels
        0,                          // FeatureLevels
        D3D11_SDK_VERSION,          // SDKVersion
        &swapChainDesc_,            // pSwapChainDesc
        &swapChain_,                // ppSwapChain
        &device_,                   // ppDevice
        &feature_level,             // pFeatureLevel
        &deviceContext_);           // ppImmediateContext

    if (FAILED(hr))
        Err::Exit("Failed to create device and swap chain.");

    swapChain_->GetBuffer(
            0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&renderTarget_));
    device_->CreateRenderTargetView(
            reinterpret_cast<ID3D11Resource*>(renderTarget_), nullptr, &renderTargetView_);

    D3D11_TEXTURE2D_DESC ds_desc = {
        .Width = static_cast<UINT>(size.x),
        .Height = static_cast<UINT>(size.y),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_D24_UNORM_S8_UINT,
        .SampleDesc = swapChainDesc_.SampleDesc,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_DEPTH_STENCIL,
    };
    device_->CreateTexture2D(&ds_desc, nullptr, &depthStencilBuffer_);

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
        .Format = ds_desc.Format,
        .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS,
    };
    device_->CreateDepthStencilView(
            reinterpret_cast<ID3D11Resource*>(depthStencilBuffer_),
            &dsv_desc,
            &depthStencilView_);
}

void AppMain::destroyDrawable() {
    safeRelease(&renderTarget_);
    safeRelease(&renderTargetView_);
    safeRelease(&depthStencilBuffer_);
    safeRelease(&depthStencilView_);
}

void AppMain::createStatusIcon() {
    auto iconData = Resource::getStatusIconData();
    hStatusIcon_ = CreateIconFromResource(
            const_cast<PBYTE>(iconData.data()),
            iconData.length(),
            TRUE,
            0x00030000);
    if (!hStatusIcon_) {
        Err::Log("Failed to load icon.");
    }
}

DWORD WINAPI AppMain::showMenu(LPVOID param) {
    constexpr DWORD winStyle = WS_CHILD;
    constexpr PCWSTR wcName = L"yoMMD-menu-window";
    AppMain& app = *reinterpret_cast<AppMain*>(param);
    const HWND& parentWin = app.hwnd_;
    HWND hwnd;

    const LONG parentWinExStyle = GetWindowLongW(parentWin, GWL_EXSTYLE);
    if (parentWinExStyle == 0) {
        Info::Log("Failed to get parent window's style");
    }

    // TODO: Register once
    WNDCLASSW wc = {};

    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = DefWindowProcW,
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = wcName;
    wc.hIcon         = LoadIcon(nullptr, IDI_WINLOGO);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    hwnd = CreateWindowExW(
            0, wcName, L"", winStyle, 0, 0, 0, 0, parentWin, nullptr,
            GetModuleHandleW(nullptr), parentWin
            );
    if (!hwnd) {
        Err::Log("Failed to create dummy window for menu.");
        return 1;
    }

    POINT point;
    if (!GetCursorPos(&point)) {
        Err::Log("Failed to get mouse point");
        DestroyWindow(hwnd);
        UnregisterClassW(wcName, GetModuleHandleW(nullptr));
        return 1;
    }

#define Cmd(identifier) static_cast<std::underlying_type<Command>::type>(Command::identifier)
    enum class Command : UINT_PTR {
        None,
        EnableMouse,
        ResetPosition,
        Quit,
    };

    HMENU hmenu = CreatePopupMenu();
    AppendMenuW(hmenu, MF_STRING, Cmd(EnableMouse), L"&Enable Mouse");
    AppendMenuW(hmenu, MF_STRING, Cmd(ResetPosition), L"&Reset Position");
    AppendMenuW(hmenu, MF_SEPARATOR, Cmd(None), L"");
    AppendMenuW(hmenu, MF_STRING, Cmd(Quit), L"&Quit");

    if (parentWinExStyle == 0) {
        EnableMenuItem(hmenu, Cmd(EnableMouse), MF_DISABLED);
    } else if (parentWinExStyle & WS_EX_TRANSPARENT) {
        CheckMenuItem(hmenu, Cmd(EnableMouse), MF_UNCHECKED);
    } else {
        CheckMenuItem(hmenu, Cmd(EnableMouse), MF_CHECKED);
    }

    UINT menuFlags = TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD;

    SetForegroundWindow(hwnd);
    auto cmdID = TrackPopupMenu(
            hmenu, menuFlags, point.x, point.y, 0, hwnd, NULL);

    switch (cmdID) {
    case Cmd(EnableMouse):
        if (parentWinExStyle != 0) {
            SetWindowLongW(parentWin, GWL_EXSTYLE,
                    parentWinExStyle ^ WS_EX_TRANSPARENT);
            // Call SetWindowPos() function?
        }
        break;
    case Cmd(ResetPosition):
        app.routine_.ResetModelPosition();
        break;
    case Cmd(Quit):
        SendMessageW(parentWin, WM_DESTROY, 0, 0);
        break;
    }

    DestroyWindow(hwnd);
#undef Cmd

    UnregisterClassW(wcName, GetModuleHandleW(nullptr));
    DestroyMenu(hmenu);
    return 0;
}

template <typename T> void AppMain::safeRelease(T **obj) {
    if (*obj) {
        (*obj)->Release();
        *obj = nullptr;
    }
}

LRESULT CALLBACK AppMain::windowProc(
        HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    using This_T = AppMain;
    This_T *pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate =
            reinterpret_cast<CREATESTRUCT *>(lParam);
        pThis = static_cast<This_T *>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->hwnd_ = hwnd;
    } else {
        pThis = reinterpret_cast<This_T *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
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
    case WM_RBUTTONDOWN:
        {
            DWORD exitCode;
            if (GetExitCodeThread(hMenuThread_, &exitCode) &&
                    exitCode == STILL_ACTIVE) {
                Info::Log("Thread is running");
                return 0;
            }

            hMenuThread_ = CreateThread(
                    NULL, 0, AppMain::showMenu, this, 0, NULL);
            return 0;
        }
    default:
        return DefWindowProc(hwnd_, uMsg, wParam, lParam);
    }
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
    glm::vec2 size(getWindowSize());
    return glm::vec2(pos.x, size.y - pos.y);  // Make origin bottom-left.
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
}

int WINAPI WinMain(
        HINSTANCE hInstance, HINSTANCE, LPSTR pCmdLine, int nCmdShow) {
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

    return 0;
}
