#include "platform_win32.hpp"
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <cyui/Core.h>
#include <commdlg.h>
#include <cstring>
#include <optional>
#include <cmath>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace cyuediff::desktop {
namespace {
void require(HRESULT result, const char *operation) {
    if (FAILED(result))
        throw Error("desktop-d3d11", std::string(operation) + " (HRESULT " +
                                         std::to_string(static_cast<unsigned long>(result)) + ")");
}
} // namespace
std::string utf8(const std::wstring &text) {
    if (text.empty())
        return {};
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (!bytes)
        throw Error("desktop-unicode", "Invalid UTF-16 text");
    std::string result(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), bytes, nullptr, nullptr);
    return result;
}
std::wstring wide(const std::string &text) {
    if (text.empty())
        return {};
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                    nullptr, 0);
    if (!chars)
        throw Error("desktop-unicode", "Invalid UTF-8 text");
    std::wstring result(chars, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), chars);
    return result;
}
std::wstring display_wide(const char *text) noexcept {
    try {
        const std::string value = text ? text : "";
        if (value.empty())
            return {};
        // UTF-8 first; MSVC filesystem/system_error messages use the ANSI code page instead.
        for (auto page : {CP_UTF8, CP_ACP}) {
            const auto flags = page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
            const int chars =
                MultiByteToWideChar(page, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (!chars)
                continue;
            std::wstring result(chars, L'\0');
            MultiByteToWideChar(page, flags, value.data(), static_cast<int>(value.size()), result.data(), chars);
            return result;
        }
    } catch (...) {
    }
    return L"Unexpected error";
}
namespace {
BOOL CALLBACK monitor_area(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto &areas = *reinterpret_cast<std::vector<WindowWorkArea> *>(data);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (areas.size() < 256 && GetMonitorInfoW(monitor, &info)) {
        const auto &r = info.rcWork;
        areas.push_back({r.left, r.top, r.right - r.left, r.bottom - r.top,
                         int(std::lround(96 * ImGui_ImplWin32_GetDpiScaleForMonitor(monitor)))});
    }
    return TRUE;
}
} // namespace
NativeWindow::NativeWindow(bool hidden, const std::optional<WindowPlacement> &placement) : hidden_(hidden) {
    ImGui_ImplWin32_EnableDpiAwareness();
    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSEXW definition{};
    definition.cbSize = sizeof(definition);
    definition.lpfnWndProc = procedure;
    definition.hInstance = instance_;
    definition.lpszClassName = L"CyUEDiffDesktop";
    definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&definition))
        throw Error("desktop-window", "Cannot register window class");
    RECT rectangle{0, 0, 1360, 900};
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    handle = CreateWindowExW(0, definition.lpszClassName, L"CyUEDiff", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                             CW_USEDEFAULT, rectangle.right - rectangle.left,
                             rectangle.bottom - rectangle.top, nullptr, nullptr, instance_, this);
    if (!handle) {
        UnregisterClassW(definition.lpszClassName, instance_);
        throw Error("desktop-window", "Cannot create window");
    }
    if (placement) {
        std::vector<WindowWorkArea> areas;
        EnumDisplayMonitors(nullptr, nullptr, monitor_area, reinterpret_cast<LPARAM>(&areas));
        if (!areas.empty()) {
            const auto fitted = fit_window_placement(*placement, areas);
            SetWindowPos(handle, nullptr, fitted.x, fitted.y, fitted.width, fitted.height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            last_placement_ = fitted;
            placement_restored = true;
        }
    }
    remember_placement();
    if (!hidden) {
        ShowWindow(handle, placement_restored && placement->maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
        UpdateWindow(handle);
    }
}
void NativeWindow::remember_placement() {
    if (!handle || !IsWindow(handle) || IsIconic(handle) || IsZoomed(handle))
        return;
    RECT r{};
    if (GetWindowRect(handle, &r)) {
        WindowPlacement p{r.left,
                          r.top,
                          r.right - r.left,
                          r.bottom - r.top,
                          int(std::lround(96 * ImGui_ImplWin32_GetDpiScaleForHwnd(handle))),
                          false};
        // Hidden validation keeps maximized intent without showing or maximizing a test window.
        if (hidden_ && last_placement_)
            p.maximized = last_placement_->maximized;
        last_placement_ = p;
    }
}
std::optional<WindowPlacement> NativeWindow::placement() const {
    auto result = last_placement_;
    if (result && handle && IsWindow(handle) && !hidden_)
        result->maximized =
            IsZoomed(handle) || (IsIconic(handle) && ([](HWND h) {
                                     WINDOWPLACEMENT p{sizeof(WINDOWPLACEMENT)};
                                     return GetWindowPlacement(h, &p) && (p.flags & WPF_RESTORETOMAXIMIZED);
                                 })(handle));
    return result;
}
void NativeWindow::reset_placement() {
    std::vector<WindowWorkArea> areas;
    EnumDisplayMonitors(nullptr, nullptr, monitor_area, reinterpret_cast<LPARAM>(&areas));
    if (areas.empty())
        return;
    const auto fitted = fit_window_placement(WindowPlacement{}, areas);
    if (!hidden_ && (IsZoomed(handle) || IsIconic(handle)))
        ShowWindow(handle, SW_RESTORE);
    last_placement_ = fitted;
    SetWindowPos(handle, nullptr, fitted.x, fitted.y, fitted.width, fitted.height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    remember_placement();
}
NativeWindow::~NativeWindow() {
    if (handle && IsWindow(handle))
        DestroyWindow(handle);
    UnregisterClassW(L"CyUEDiffDesktop", instance_);
}
bool NativeWindow::pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT)
            quit = true;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !quit;
}
LRESULT CALLBACK NativeWindow::procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto create = reinterpret_cast<CREATESTRUCTW *>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto self = reinterpret_cast<NativeWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
        return 1;
    if (self) {
        if (message == WM_CLOSE) {
            self->remember_placement();
            self->quit = true;
            return 0;
        }
        if (message == WM_WINDOWPOSCHANGED) {
            self->remember_placement();
        }
        if (message == WM_SIZE) {
            self->minimized = wparam == SIZE_MINIMIZED;
            if (!self->minimized) {
                self->resize_width = LOWORD(lparam);
                self->resize_height = HIWORD(lparam);
            }
            return 0;
        }
        if (message == WM_DPICHANGED) {
            if (ImGui::GetCurrentContext())
                cyui::applyDpi(float(HIWORD(wparam)) / 96.f);
            auto rect = reinterpret_cast<RECT *>(lparam);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                         rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        if (message == WM_GETMINMAXINFO) {
            auto info = reinterpret_cast<MINMAXINFO *>(lparam);
            const auto scale = ImGui_ImplWin32_GetDpiScaleForHwnd(window);
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
                info->ptMinTrackSize = {
                    std::min(LONG(960 * scale), monitor.rcWork.right - monitor.rcWork.left),
                    std::min(LONG(640 * scale), monitor.rcWork.bottom - monitor.rcWork.top)};
            return 0;
        }
        if (message == WM_DESTROY) {
            self->quit = true;
            PostQuitMessage(0);
            return 0;
        }
    }
    if (message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_KEYMENU)
        return 0;
    return DefWindowProcW(window, message, wparam, lparam);
}
std::optional<fs::path> NativeWindow::choose_file(bool project, bool container, bool schema) {
    std::vector<wchar_t> buffer(32768, 0);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = handle;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    auto filter = wide(cyui::tr(
        schema      ? "Cooked schema context (*.json)"
        : container ? "Unreal container (*.pak;*.utoc)"
                    : (project ? "Unreal project (*.uproject)" : "Unreal package (*.uasset;*.umap)")));
    filter.push_back(L'\0');
    filter += schema      ? L"*.json"
              : container ? L"*.pak;*.utoc"
                          : (project ? L"*.uproject" : L"*.uasset;*.umap");
    filter.append(2, L'\0');
    dialog.lpstrFilter = filter.c_str();
    auto title = wide(cyui::tr(schema      ? "Choose schema context"
                               : container ? "Choose container"
                                           : (project ? "Choose project" : "Choose package")));
    dialog.lpstrTitle = title.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
    if (GetOpenFileNameW(&dialog))
        return fs::path(buffer.data());
    if (CommDlgExtendedError())
        throw Error("desktop-dialog", "The file dialog failed");
    return std::nullopt;
}
std::optional<fs::path> NativeWindow::choose_workspace(bool save) {
    std::vector<wchar_t> buffer(32768, 0);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = handle;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    auto filter = wide(cyui::tr("CyUEDiff workspace (*.json)"));
    filter.push_back(L'\0');
    filter += L"*.json";
    filter.append(2, L'\0');
    auto title = wide(cyui::tr(save ? "Save workspace as..." : "Choose workspace"));
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrTitle = title.c_str();
    dialog.lpstrDefExt = L"json";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
        return fs::path(buffer.data());
    if (CommDlgExtendedError())
        throw Error("desktop-dialog", "The workspace file dialog failed");
    return std::nullopt;
}
void Renderer::target() {
    ComPtr<ID3D11Texture2D> buffer;
    require(swap_->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf())), "Get back buffer");
    require(device_->CreateRenderTargetView(buffer.Get(), nullptr, target_.GetAddressOf()),
            "Create render target");
}
Renderer::Renderer(HWND window, bool software) {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    auto create = [&](D3D_DRIVER_TYPE type) {
        return D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                             &description, swap_.ReleaseAndGetAddressOf(),
                                             device_.ReleaseAndGetAddressOf(), nullptr,
                                             context_.ReleaseAndGetAddressOf());
    };
    auto result = create(software ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE);
    if (FAILED(result) && !software)
        result = create(D3D_DRIVER_TYPE_WARP);
    require(result, "Create Direct3D 11 device");
    target();
    win32_ = ImGui_ImplWin32_Init(window);
    if (!win32_)
        throw Error("desktop-backend", "Cannot initialize Win32 ImGui backend");
    dx11_ = ImGui_ImplDX11_Init(device_.Get(), context_.Get());
    if (!dx11_) {
        ImGui_ImplWin32_Shutdown();
        win32_ = false;
        throw Error("desktop-backend", "Cannot initialize DX11 ImGui backend");
    }
}
Renderer::~Renderer() {
    if (dx11_)
        ImGui_ImplDX11_Shutdown();
    if (win32_)
        ImGui_ImplWin32_Shutdown();
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
}
void Renderer::resize(UINT width, UINT height) {
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset();
    require(swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "Resize swap chain");
    target();
}
ComPtr<ID3D11ShaderResourceView> Renderer::upload(const ImageRgba8 &image) {
    if (!image.width || !image.height || image.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        image.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        image.pixels.size() != std::uint64_t(image.width) * image.height * 4)
        throw Error("desktop-image", "Invalid GPU preview dimensions");
    D3D11_TEXTURE2D_DESC description{};
    description.Width = image.width;
    description.Height = image.height;
    description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = image.pixels.data();
    data.SysMemPitch = image.width * 4;
    ComPtr<ID3D11Texture2D> texture;
    require(device_->CreateTexture2D(&description, &data, texture.GetAddressOf()), "Upload image");
    ComPtr<ID3D11ShaderResourceView> view;
    require(device_->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()),
            "Create image view");
    return view;
}
void Renderer::new_frame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}
void Renderer::draw() {
    auto background = cyui::vec(cyui::palette().bg0);
    const float color[] = {background.x, background.y, background.z, background.w};
    context_->OMSetRenderTargets(1, target_.GetAddressOf(), nullptr);
    context_->ClearRenderTargetView(target_.Get(), color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
void Renderer::present(bool synchronized) {
    require(swap_->Present(synchronized ? 1 : 0, 0), "Present frame");
}
ImageRgba8 Renderer::capture() {
    ComPtr<ID3D11Texture2D> buffer, staging;
    require(swap_->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf())), "Get capture buffer");
    D3D11_TEXTURE2D_DESC description{};
    buffer->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = description.MiscFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    require(device_->CreateTexture2D(&description, nullptr, staging.GetAddressOf()),
            "Create staging capture");
    context_->CopyResource(staging.Get(), buffer.Get());
    ImageRgba8 image;
    image.width = description.Width;
    image.height = description.Height;
    image.pixels.resize(std::size_t(image.width) * image.height * 4);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read capture");
    for (std::uint32_t y = 0; y < image.height; ++y)
        std::memcpy(image.pixels.data() + std::size_t(y) * image.width * 4,
                    static_cast<const std::uint8_t *>(mapped.pData) + std::size_t(y) * mapped.RowPitch,
                    std::size_t(image.width) * 4);
    context_->Unmap(staging.Get(), 0);
    return image;
}
} // namespace cyuediff::desktop
