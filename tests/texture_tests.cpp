#include "../src/texture.hpp"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void error(F fn, const std::string &code) {
    try {
        fn();
    } catch (const Error &e) {
        check(e.code == code, ("Expected " + code + ", got " + e.code).c_str());
        return;
    }
    throw std::runtime_error("Expected error " + code);
}
struct Bytes {
    std::vector<std::uint8_t> data;
    template <class T> void put(T value) {
        auto p = reinterpret_cast<std::uint8_t *>(&value);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void big(std::uint64_t value, unsigned count) {
        for (unsigned i = count; i > 0; --i)
            data.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
    }
    void zeros(std::size_t count) {
        data.insert(data.end(), count, 0);
    }
    void append(const Bytes &b) {
        data.insert(data.end(), b.data.begin(), b.data.end());
    }
    template <class T> void patch(std::size_t offset, T value) {
        std::memcpy(data.data() + offset, &value, sizeof(T));
    }
    void save(const fs::path &path) const {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
};
std::uint32_t crc(const Bytes &bytes, std::size_t start, std::size_t end) {
    std::uint32_t value = ~0u;
    for (auto i = start; i < end; ++i) {
        value ^= bytes.data[i];
        for (int b = 0; b < 8; ++b)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0);
    }
    return ~value;
}
const std::string identity(40, '1');
Bytes buffer(const Bytes &raw, unsigned method = 0) {
    Bytes b;
    b.big(0xb7756362, 4);
    b.zeros(4);
    b.data.push_back(static_cast<std::uint8_t>(method));
    b.zeros(3);
    b.big(1, 4);
    b.big(raw.data.size(), 8);
    b.big(64 + raw.data.size() + (method ? 4 : 0), 8);
    b.data.insert(b.data.end(), 20, 0x11);
    b.zeros(12);
    if (method) {
        b.data[11] = 3;
        b.big(raw.data.size(), 4);
    }
    auto checksum = crc(b, 8, b.data.size());
    for (unsigned i = 0; i < 4; ++i)
        b.data[4 + i] = static_cast<std::uint8_t>(checksum >> (24 - i * 8));
    b.append(raw);
    return b;
}
Bytes container(const Bytes &encoded, unsigned access = 0, unsigned version = 2) {
    Bytes b;
    b.put<std::uint32_t>(0);
    b.zeros(2);
    b.put<std::uint32_t>(528);
    b.zeros(16);
    b.data.insert(b.data.end(), 20, 0x11);
    std::uint64_t raw_size = 0;
    for (unsigned i = 0; i < 8; ++i)
        raw_size = (raw_size << 8) | encoded.data[16 + i];
    b.put(raw_size);
    b.zeros(6);
    auto begin = b.data.size();
    b.put<std::uint64_t>(0xd1c43b2e80a5f697ull);
    b.put<std::uint32_t>(version);
    auto header = version == 0 ? 73u : version == 1 ? 73u : 77u;
    b.put<std::uint32_t>(header);
    b.put<std::uint64_t>(access ? 0 : encoded.data.size());
    if (version == 0)
        b.put<std::uint8_t>(static_cast<std::uint8_t>(access));
    b.put<std::int32_t>(1);
    b.data.insert(b.data.end(), 20, 0x11);
    b.put<std::int64_t>(access == 2 ? -1 : 0);
    b.put<std::uint64_t>(encoded.data.size());
    b.put(raw_size);
    if (version == 2)
        b.zeros(4);
    if (version >= 1)
        b.put<std::uint8_t>(static_cast<std::uint8_t>(access));
    if (!access)
        b.append(encoded);
    auto length = b.data.size() - begin + 20;
    b.put<std::uint64_t>(0x29bfca045138de76ull);
    b.put<std::uint64_t>(length);
    b.put<std::uint32_t>(0x9e2a83c1);
    return b;
}
Package package(const fs::path &path, std::uint32_t w = 2, std::uint32_t h = 1, unsigned mips = 1,
                const char *format = "TSF_BGRA8", const char *compression = "TSCF_None") {
    Package p;
    p.source = path;
    p.summary.custom_versions["697dd581e64f41abaa4a51ecbeb7b628"] = 119;
    ObjectExport object;
    object.name = object.path = "Texture";
    object.class_name = "/Script/Engine.Texture2D";
    object.properties = {{"Source",
                          {{"value",
                            {{"SizeX", {{"value", w}}},
                             {"SizeY", {{"value", h}}},
                             {"NumMips", {{"value", mips}}},
                             {"NumSlices", {{"value", 1}}},
                             {"Format", {{"value", format}}},
                             {"CompressionFormat", {{"value", compression}}},
                             {"LayerFormat", {{"value", Json::array({format})}}},
                             {"BlockDataOffsets", {{"value", Json::array({0})}}}}}}},
                         {"SRGB", {{"value", true}}}};
    p.exports.push_back(object);
    return p;
}
TextureSource decode_native(const Package &p) {
    ReadOptions options;
    detail::Archive a(p.source, options);
    auto result = detail::decode_texture_source(a, p, 0, 60, options);
    check(result.end == 60, "Texture native decoder stops at object boundary");
    return result.source;
}
} // namespace
int main() {
    auto root =
        fs::current_path() /
        (".test-tmp-texture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        auto path = root / "source.uasset";
        Bytes raw;
        raw.data = {0, 0, 255, 255, 0, 255, 0, 128};
        auto original = container(buffer(raw));
        original.save(path);
        auto p = package(path);
        auto texture = decode_native(p);
        auto image = decode_texture(texture);
        check(image.width == 2 && image.pixels == std::vector<std::uint8_t>({255, 0, 0, 255, 0, 255, 0, 128}),
              "BGRA channels and alpha preserved");
        TextureDecodeOptions cancel_texture;
        cancel_texture.control.cancelled = [] { return true; };
        error([&] { decode_texture(texture, 0, cancel_texture); }, "cancelled");
        check(texture.payload.offset == 137 && texture.payload.raw_bytes == 8 &&
                  texture.payload.storage == "local",
              "Validated trailer resolves source bytes");
        check(!texture.payload.sha256.empty() && texture.to_json().at("settings").contains("SRGB"),
              "Payload fingerprint and texture settings");
        check(!texture.to_json().at("payload").contains("file") &&
                  texture.to_json(true).at("payload").contains("file"),
              "Physical paths omitted from semantic snapshots");
        p.textures.push_back(texture);
        auto q = p;
        q.textures[0].settings["SRGB"]["value"] = false;
        auto diff = compare(p, q);
        check(diff.status() == "changed" && !diff.texture_changes.empty(),
              "Texture setting has dedicated semantic diff");
        check(!q.semantic_snapshot().at("textures").empty(), "Textconv includes source payload fingerprint");
        auto identical = compare_images(image, image, 0, true);
        check(identical.changed_pixels == 0 && identical.heatmap.pixels[3] == 0,
              "Identical mask is transparent");
        auto changed = image;
        changed.pixels[0] = 250;
        changed.pixels[7] = 127;
        auto delta = compare_images(image, changed, 0, true);
        check(delta.changed_pixels == 2 && delta.max_channel_delta == 5 && delta.mean_absolute_error == 0.75,
              "RGB and alpha deltas measured");
        check(delta.heatmap.pixels[0] == 255 && delta.heatmap.pixels[3] == 255, "Changed pixel highlighted");
        check(compare_images(image, changed, 1).changed_pixels == 1 &&
                  compare_images(image, changed, 5).changed_pixels == 0,
              "Threshold counts max RGBA delta strictly above threshold");
        ImageRgba8 small{1, 1, {0, 0, 0, 255}};
        auto dimensions = compare_images(image, small);
        check(!dimensions.comparable && dimensions.to_json().at("changedPixels").is_null(),
              "Dimension change does not invent pixel correspondence");
        error([&] { compare_images(image, small, 0, true); }, "image-size");
        error([&] { compare_images(image, ImageRgba8{}); }, "image-layout");
        error([&] { decode_texture(texture, 1); }, "texture-mip");
        TextureDecodeOptions limits;
        limits.max_pixels = 1;
        error([&] { decode_texture(texture, 0, limits); }, "limit");
        limits = {};
        limits.max_payload_bytes = 4;
        error([&] { decode_texture(texture, 0, limits); }, "limit");
        auto changed_file = original;
        changed_file.data[201] ^= 1;
        changed_file.save(path);
        error([&] { decode_texture(texture); }, "source-changed");
        original.save(path);
        auto bad = original;
        bad.data[141] ^= 1;
        bad.save(path);
        error([&] { decode_native(p); }, "texture-buffer-crc");
        bad = original;
        bad.patch<std::uint64_t>(165, 9);
        bad.save(path);
        error([&] { decode_native(p); }, "texture-buffer");
        bad = original;
        bad.patch<std::int64_t>(108, 999);
        bad.save(path);
        error([&] { decode_native(p); }, "texture-trailer");
        bad = original;
        bad.patch<std::uint32_t>(68, 3);
        bad.save(path);
        error([&] { decode_native(p); }, "texture-trailer-version");
        bad = original;
        bad.patch<std::uint64_t>(bad.data.size() - 12, 999999);
        bad.save(path);
        error([&] { decode_native(p); }, "texture-trailer");
        original.save(path);
        ReadOptions options;
        {
            detail::Archive archive(path, options);
            error([&] { detail::decode_texture_source(archive, p, 0, 59, options); }, "bounds");
        }
        auto future = p;
        future.summary.custom_versions["697dd581e64f41abaa4a51ecbeb7b628"] = 120;
        error([&] { decode_native(future); }, "texture-profile");
        for (unsigned version = 0; version < 3; ++version) {
            container(buffer(raw), 0, version).save(path);
            check(decode_texture(decode_native(p)).pixels == image.pixels,
                  "Supported trailer version resolves identical pixels");
        }
        container(buffer(raw), 2).save(path);
        auto virtualized = decode_native(p);
        check(virtualized.payload.storage == "virtualized",
              "Virtualized source identified without invented bytes");
        error([&] { decode_texture(virtualized); }, "texture-payload");
        container(buffer(raw), 1).save(path);
        check(decode_native(p).payload.storage == "referenced", "Referenced source stays explicit");
        Bytes empty;
        empty.zeros(60);
        empty.save(path);
        check(detail::locate_texture_payload(path, identity, 8, options).storage == "unavailable",
              "No signature scan when trailer missing");
        container(buffer(raw, 4)).save(path);
        auto lz4 = decode_native(p);
        check(lz4.payload.method == 4, "Compressed buffer method reported");
        error([&] { decode_texture(lz4); }, "texture-codec");
        original.save(path);
        auto missing = p;
        missing.exports[0].properties["Source"]["value"]["LayerFormat"] = Json{{"opaque", {{"bytes", 12}}}};
        check(detail::texture_metadata(missing.exports[0]).layers == 0,
              "Opaque layer count does not imply one layer");
        auto overflow = texture;
        overflow.width = 0xffffffffu;
        overflow.height = 0xffffffffu;
        overflow.mips = 32;
        error([&] { decode_texture(overflow, 31); }, "limit");
        auto multi = texture;
        multi.layers = 2;
        error([&] { decode_texture(multi); }, "texture-layout");
        multi = texture;
        multi.format = "TSF_RGBA16F";
        error([&] { decode_texture(multi); }, "texture-format");
        Bytes mips;
        mips.data = {10, 20, 30, 40, 90};
        container(buffer(mips)).save(path);
        auto mip_texture = decode_native(package(path, 2, 2, 2, "TSF_G8"));
        auto mip_image = decode_texture(mip_texture, 1);
        check(mip_image.width == 1 && mip_image.pixels == std::vector<std::uint8_t>({90, 90, 90, 255}),
              "Raw mip selection uses full chain offsets");
        Bytes sixteen;
        sixteen.data = {0x34, 0x12};
        container(buffer(sixteen)).save(path);
        check(decode_texture(decode_native(package(path, 1, 1, 1, "TSF_G16"))).pixels ==
                  std::vector<std::uint8_t>({0x12, 0x12, 0x12, 255}),
              "16 bit preview uses high byte");
        auto png_path = root / fs::u8path("aperçu.png");
        write_png(png_path, image);
        Bytes png_bytes;
        {
            std::ifstream file(png_path, std::ios::binary);
            png_bytes.data.assign(std::istreambuf_iterator<char>(file), {});
        }
        container(buffer(png_bytes)).save(path);
        auto png_texture = decode_native(package(path, 2, 1, 1, "TSF_BGRA8", "TSCF_PNG"));
        check(decode_texture(png_texture).pixels == image.pixels, "PNG preview roundtrip with Unicode path");
        limits = {};
        limits.max_decoder_bytes = 1;
        error([&] { decode_texture(png_texture, 0, limits); }, "limit");
        auto wrong = png_texture;
        wrong.width = 3;
        error([&] { decode_texture(wrong); }, "texture-dimensions");
        Bytes jpg;
        auto callback = [](void *target, void *data, int count) {
            auto &bytes = *static_cast<Bytes *>(target);
            auto p = static_cast<std::uint8_t *>(data);
            bytes.data.insert(bytes.data.end(), p, p + count);
        };
        std::vector<std::uint8_t> red(8 * 8 * 3);
        for (std::size_t i = 0; i < red.size(); i += 3)
            red[i] = 255;
        check(stbi_write_jpg_to_func(callback, &jpg, 8, 8, 3, red.data(), 95) != 0, "JPEG fixture encoded");
        container(buffer(jpg)).save(path);
        auto jpeg_image = decode_texture(decode_native(package(path, 8, 8, 1, "TSF_BGRA8", "TSCF_JPEG")));
        check(jpeg_image.pixels[0] >= 250 && jpeg_image.pixels[1] < 5 && jpeg_image.pixels[2] < 5 &&
                  jpeg_image.pixels[3] == 255,
              "JPEG source decoder preserves expected red");
        check(sha256_bytes(std::vector<std::uint8_t>{'a', 'b', 'c'}) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "Byte fingerprint known vector");
        if (root.parent_path() != fs::current_path())
            throw std::runtime_error("Invalid fixture cleanup root");
        fs::remove_all(root);
        std::cout << checks << " texture checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << "; fixtures: " << root << '\n';
        return 1;
    }
}
