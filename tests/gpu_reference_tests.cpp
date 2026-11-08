// Independent platform reference: D3D11 WARP samples BC blocks through a pixel shader.
// This test-only dependency is never linked into Core or its SDK consumer.
#include <cyuediff/gpu_texture.hpp>
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <iostream>
using namespace cyuediff;
using Microsoft::WRL::ComPtr;
namespace {
void ok(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("D3D11 reference operation failed: " + std::to_string(hr));
}
ComPtr<ID3DBlob> shader(const char *entry, const char *target) {
    const char source[] = R"(
Texture2D<float4> image : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
 return float4(id==2?3:-1,id==1?3:-1,0,1);
}
float4 ps(float4 position : SV_Position) : SV_Target { return image.Load(int3(int2(position.xy),0)); }
)";
    ComPtr<ID3DBlob> blob, error;
    ok(D3DCompile(source, sizeof(source) - 1, nullptr, nullptr, nullptr, entry, target,
                  D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &error));
    return blob;
}
} // namespace
int main() {
    try {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level;
        ok(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                             &device, &level, &context));
        auto vb = shader("vs", "vs_5_0"), pb = shader("ps", "ps_5_0");
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        ok(device->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &vs));
        ok(device->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &ps));
        struct Pair {
            GpuPixelFormat core;
            DXGI_FORMAT dxgi;
        };
        const Pair formats[] = {{GpuPixelFormat::bc1_unorm, DXGI_FORMAT_BC1_UNORM},
                                {GpuPixelFormat::bc2_unorm, DXGI_FORMAT_BC2_UNORM},
                                {GpuPixelFormat::bc3_unorm, DXGI_FORMAT_BC3_UNORM},
                                {GpuPixelFormat::bc4_unorm, DXGI_FORMAT_BC4_UNORM},
                                {GpuPixelFormat::bc5_unorm, DXGI_FORMAT_BC5_UNORM},
                                {GpuPixelFormat::bc6h_ufloat, DXGI_FORMAT_BC6H_UF16},
                                {GpuPixelFormat::bc7_unorm, DXGI_FORMAT_BC7_UNORM}};
        unsigned checks = 0;
        float max_ldr = 0, max_hdr = 0;
        for (auto pair : formats) {
            constexpr unsigned width = 128, height = 64;
            std::vector<std::uint8_t> encoded(
                static_cast<std::size_t>(gpu_texture_bytes(pair.core, width, height)));
            std::uint32_t random = 0x918af031;
            for (auto &byte : encoded) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                byte = random & 255;
            }
            auto decoded = decode_gpu_rgba32f(encoded, width, height, pair.core);
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = pair.dxgi;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = encoded.data();
            data.SysMemPitch = static_cast<UINT>(encoded.size() / (height / 4));
            ComPtr<ID3D11Texture2D> source;
            ok(device->CreateTexture2D(&desc, &data, &source));
            ComPtr<ID3D11ShaderResourceView> srv;
            ok(device->CreateShaderResourceView(source.Get(), nullptr, &srv));
            desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> target;
            ok(device->CreateTexture2D(&desc, nullptr, &target));
            ComPtr<ID3D11RenderTargetView> rtv;
            ok(device->CreateRenderTargetView(target.Get(), nullptr, &rtv));
            auto *rv = rtv.Get();
            auto *sv = srv.Get();
            context->OMSetRenderTargets(1, &rv, nullptr);
            D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
            context->RSSetViewports(1, &viewport);
            context->VSSetShader(vs.Get(), nullptr, 0);
            context->PSSetShader(ps.Get(), nullptr, 0);
            context->PSSetShaderResources(0, 1, &sv);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->Draw(3, 0);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            ok(device->CreateTexture2D(&desc, nullptr, &staging));
            context->CopyResource(staging.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            ok(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
            for (unsigned y = 0; y < height; ++y) {
                auto *row = reinterpret_cast<const float *>(static_cast<const std::uint8_t *>(mapped.pData) +
                                                            y * mapped.RowPitch);
                for (unsigned x = 0; x < width; ++x)
                    for (unsigned c = 0; c < 4; ++c) {
                        const float expected =
                            pair.core == GpuPixelFormat::bc4_unorm && c < 3 ? row[x * 4] : row[x * 4 + c];
                        const float actual = decoded.pixels[(y * width + x) * 4 + c];
                        const float delta = std::abs(actual - expected);
                        const bool hdr = pair.core == GpuPixelFormat::bc6h_ufloat;
                        const float tolerance = hdr ? std::max(0.000001f, std::abs(expected) * 0.0011f)
                                                    : (pair.core == GpuPixelFormat::bc1_unorm ||
                                                               pair.core == GpuPixelFormat::bc2_unorm ||
                                                               pair.core == GpuPixelFormat::bc3_unorm
                                                           ? 3.01f
                                                           : 1.01f) /
                                                          255.f;
                        (hdr ? max_hdr : max_ldr) = std::max(hdr ? max_hdr : max_ldr, delta);
                        if (!std::isfinite(expected) || delta > tolerance) {
                            context->Unmap(staging.Get(), 0);
                            throw std::runtime_error(
                                std::string(gpu_pixel_format_name(pair.core)) + " platform mismatch at " +
                                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(c) + ": " +
                                std::to_string(actual) + " vs " + std::to_string(expected));
                        }
                        ++checks;
                    }
            }
            context->Unmap(staging.Get(), 0);
            std::cout << gpu_pixel_format_name(pair.core)
                      << ": 512 independently sampled random blocks passed\n";
        }
        std::cout << checks << " WARP reference channels passed; maximum UNORM delta " << max_ldr
                  << ", HDR absolute delta " << max_hdr << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
