#include <cyuediff/hdr.hpp>
#include "package_fixture.hpp"
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void error(F f, const char *code) {
    try {
        f();
        throw std::runtime_error("Expected failure");
    } catch (const Error &e) {
        check(e.code == code, "Expected HDR error code");
    }
}
TextureSource source(const fs::path &path, const std::string &format, const Bytes &raw, unsigned width,
                     unsigned mips = 1) {
    Bytes bytes;
    bytes.zeros(64);
    bytes.data[0] = 0xb7;
    bytes.data[1] = 0x75;
    bytes.data[2] = 0x63;
    bytes.data[3] = 0x62;
    bytes.data[15] = 1;
    auto big = [&](std::size_t at, std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i)
            bytes.data[at + 7 - i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    big(16, raw.data.size());
    big(24, raw.data.size() + 64);
    std::uint32_t crc = ~0u;
    for (std::size_t i = 8; i < 64; ++i) {
        crc ^= bytes.data[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    crc ^= ~0u;
    for (unsigned i = 0; i < 4; ++i)
        bytes.data[7 - i] = static_cast<std::uint8_t>(crc >> (8 * i));
    bytes.append(raw);
    bytes.save(path);
    TextureSource texture;
    texture.object_path = "HDR";
    texture.format = format;
    texture.width = width;
    texture.height = 1;
    texture.mips = mips;
    texture.slices = texture.layers = 1;
    texture.block_offsets = {0};
    texture.payload.file = path;
    texture.payload.storage = "local";
    texture.payload.raw_bytes = raw.data.size();
    texture.payload.stored_bytes = bytes.data.size();
    return texture;
}
} // namespace
int main() {
    auto root =
        fs::current_path() /
        (".test-tmp-hdr-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        auto path = root / "raw.bin";
        Bytes half;
        for (std::uint16_t value : {0x3c00, 0x4000, 0x3800, 0x3c00, 0xb400, 0x0001, 0x7bff, 0x0000})
            half.put(value);
        auto texture = source(path, "TSF_RGBA16F", half, 2);
        auto decoded = decode_texture_hdr(texture);
        check(decoded.pixels[0] == 1.f && decoded.pixels[1] == 2.f && decoded.pixels[2] == .5f,
              "Half-float dynamic range retained");
        check(decoded.pixels[4] == -.25f && decoded.pixels[5] == std::ldexp(1.f, -24) &&
                  decoded.pixels[6] == 65504.f,
              "Negative, subnormal and maximum finite half samples");
        auto preview = tone_map_hdr(decoded);
        check(preview.pixels[0] == 188 && preview.pixels[1] == 213 && preview.pixels[2] == 156 &&
                  preview.pixels[3] == 255,
              "Documented Reinhard/sRGB display conversion");
        check(preview.pixels[4] == 0 && preview.pixels[6] == 255 && preview.pixels[7] == 0,
              "Display clamps negative RGB and preserves alpha");
        check(decode_texture(texture).pixels == preview.pixels,
              "Existing preview API supports HDR without changing raw samples");
        auto brighter = tone_map_hdr(decoded, 1);
        check(brighter.pixels[0] > preview.pixels[0] && decoded.pixels[1] == 2.f,
              "Exposure changes display only");
        TextureDecodeOptions budget;
        budget.max_decoder_bytes = 16;
        error([&] { decode_texture_hdr(texture, 0, budget); }, "limit");
        error([&] { tone_map_hdr(decoded, 33); }, "hdr-exposure");
        auto changed = decoded;
        changed.pixels[1] += .125f;
        auto diff = compare_hdr_images(decoded, changed, 0, true);
        check(diff.changed_pixels == 1 && diff.max_sample_delta == .125 &&
                  diff.mean_absolute_error == .015625,
              "Native sample delta and error");
        check(diff.heatmap.pixels[0] == 255 && diff.heatmap.pixels[7] == 0,
              "HDR mask marks source-sample changes");
        check(compare_hdr_images(decoded, changed, .125).changed_pixels == 0,
              "HDR threshold uses source units");
        ImageRgba32f large{1, 1, {65504, 65504, 65504, 1}}, larger = large;
        larger.pixels[0] += 1;
        check(tone_map_hdr(large).pixels == tone_map_hdr(larger).pixels &&
                  compare_hdr_images(large, larger).changed_pixels == 1,
              "Changes hidden by an 8-bit display remain visible in HDR comparison");
        check(diff.to_json()["maxSampleDelta"] == .125 && diff.to_json()["schema"] == "cyuediff.hdr-diff/1",
              "HDR report declares its sample domain");
        check(!compare_hdr_images(decoded, large).comparable,
              "Different dimensions have no invented correspondence");
        error([&] { compare_hdr_images(decoded, large, 0, true); }, "image-size");
        error([&] { compare_hdr_images(decoded, changed, -1); }, "hdr-threshold");
        Bytes single;
        for (float f : {1.f, -2.f, 0.f, 400000.f})
            single.put(f);
        auto single_image = decode_texture_hdr(source(path, "TSF_R32F", single, 4));
        check(single_image.pixels[0] == 1 && single_image.pixels[1] == 1 && single_image.pixels[4] == -2 &&
                  single_image.pixels[12] == 400000,
              "R32F channels and range retained");
        Bytes rgba;
        for (float f : {1.f, 2.f, -3.f, .25f})
            rgba.put(f);
        check(decode_texture_hdr(source(path, "TSF_RGBA32F", rgba, 1)).pixels ==
                  std::vector<float>({1, 2, -3, .25}),
              "RGBA32F exact samples");
        Bytes rgbe;
        rgbe.data = {0, 64, 128, 129};
        auto packed = decode_texture_hdr(source(path, "TSF_BGRE8", rgbe, 1));
        check(packed.pixels[0] == 1.00390625f && packed.pixels[1] == .50390625f &&
                  packed.pixels[2] == .00390625f,
              "BGRE mantissa midpoint and exponent");
        Bytes zero;
        zero.data = {255, 255, 255, 0};
        check(decode_texture_hdr(source(path, "TSF_BGRE8", zero, 1)).pixels ==
                  std::vector<float>({0, 0, 0, 1}),
              "Zero RGBE exponent decodes black");
        Bytes mip;
        for (std::uint16_t v : {0x3c00, 0x4000, 0x4200})
            mip.put(v);
        auto selected = decode_texture_hdr(source(path, "TSF_R16F", mip, 2, 2), 1);
        check(selected.width == 1 && selected.pixels[0] == 3, "HDR source mip offsets");
        Bytes nonfinite;
        nonfinite.put<std::uint16_t>(0x7c00);
        auto invalid = source(path, "TSF_R16F", nonfinite, 1);
        error([&] { decode_texture_hdr(invalid); }, "hdr-sample");
        OperationControl cancelled;
        cancelled.cancelled = [] { return true; };
        error([&] { tone_map_hdr(decoded, 0, cancelled); }, "cancelled");
        error([&] { compare_hdr_images(decoded, changed, 0, false, cancelled); }, "cancelled");
        check(is_hdr_format("TSF_RGBA16F") && !is_hdr_format("TSF_BGRA8"), "HDR format classification");
        fs::remove_all(root);
        std::cout << checks << " HDR checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nFixtures: " << root << '\n';
        return 1;
    }
}
