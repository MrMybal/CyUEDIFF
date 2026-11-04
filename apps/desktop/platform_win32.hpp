#pragma once
#include <cyuediff/core.hpp>
#include <imgui.h>
#include <optional>
#include <d3d11.h>
#include <wrl/client.h>
#include <windows.h>
namespace cyuediff::desktop {
using Microsoft::WRL::ComPtr;
std::string utf8(const std::wstring &);
std::wstring wide(const std::string &);
class NativeWindow {
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    HINSTANCE instance_ = nullptr;

  public:
    HWND handle = nullptr;
    UINT resize_width = 0, resize_height = 0;
    bool quit = false, minimized = false;
    NativeWindow(bool hidden);
    ~NativeWindow();
    NativeWindow(const NativeWindow &) = delete;
    NativeWindow &operator=(const NativeWindow &) = delete;
    bool pump();
    std::optional<fs::path> choose_file(bool project = false);
};
class Renderer {
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swap_;
    ComPtr<ID3D11RenderTargetView> target_;
    bool win32_ = false, dx11_ = false;
    void target();

  public:
    Renderer(HWND, bool software);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void resize(UINT, UINT);
    ComPtr<ID3D11ShaderResourceView> upload(const ImageRgba8 &);
    void new_frame();
    void draw();
    void present(bool synchronized);
    ImageRgba8 capture();
};
} // namespace cyuediff::desktop
