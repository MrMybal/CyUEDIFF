#include "texture.hpp"
#include <iomanip>
#include <sstream>
#include <set>
namespace cyuediff::detail {
namespace {
std::string hex_bytes(Archive &archive, std::size_t count) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < count; ++i)
        out << std::setw(2) << unsigned(archive.get<std::uint8_t>());
    return out.str();
}
std::uint64_t unsigned_size(Archive &a) {
    auto n = a.get<std::int64_t>();
    if (n < 0)
        throw Error("texture-size", "Negative texture payload size", a.tell() - 8);
    return static_cast<std::uint64_t>(n);
}
std::uint64_t big(Archive &a, unsigned bytes) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
        value = (value << 8) | a.get<std::uint8_t>();
    return value;
}
std::uint32_t crc32(const std::vector<std::uint8_t> &data) {
    std::uint32_t value = 0xffffffffu;
    for (auto byte : data) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^
                    (0xedb88320u & static_cast<std::uint32_t>(-static_cast<std::int32_t>(value & 1)));
    }
    return value ^ 0xffffffffu;
}
void buffer_header(Archive &a, TexturePayload &out, std::uint64_t limit) {
    a.seek(out.offset);
    Archive::ReadLimit window(a, limit);
    if (big(a, 4) != 0xb7756362)
        throw Error("texture-buffer", "Invalid compressed buffer magic", out.offset);
    auto checksum = big(a, 4);
    out.method = a.get<std::uint8_t>();
    a.skip(3);
    auto blocks = big(a, 4), raw = big(a, 8), stored = big(a, 8);
    auto hash = hex_bytes(a, 32);
    if (raw != out.raw_bytes || stored < 64 || stored > limit - out.offset ||
        (out.stored_bytes && out.stored_bytes != stored))
        throw Error("texture-buffer", "Inconsistent compressed buffer sizes", out.offset);
    if (!out.content_id.empty() && hash.substr(0, 40) != out.content_id)
        throw Error("texture-buffer", "Payload identity disagrees with compressed buffer", out.offset);
    if (out.method != 0 && out.method != 3 && out.method != 4)
        throw Error("texture-buffer", "Unknown compressed buffer method", out.offset + 8);
    if (out.method == 0 && (blocks != 1 || stored != raw + 64))
        throw Error("texture-buffer", "Invalid uncompressed buffer layout", out.offset);
    std::uint64_t header_size = 64;
    if (out.method != 0) {
        if (blocks > 1000000)
            throw Error("limit", "Compressed block table exceeds supported limit", out.offset);
        if (blocks > (limit - out.offset - 64) / 4)
            throw Error("texture-buffer", "Invalid block table", out.offset);
        header_size += blocks * 4;
    }
    a.seek(out.offset + 8);
    std::vector<std::uint8_t> header(static_cast<std::size_t>(header_size - 8));
    a.read(header.data(), header.size());
    if (crc32(header) != checksum)
        throw Error("texture-buffer-crc", "Compressed buffer header CRC mismatch", out.offset + 4);
    out.stored_bytes = stored;
}
TexturePayload trailer(Archive &a, const fs::path &path, const std::string &id, std::uint64_t raw,
                       const ReadOptions &) {
    TexturePayload result;
    result.content_id = id;
    result.raw_bytes = raw;
    if (a.size() < 48)
        return result;
    a.seek(a.size() - 20);
    auto tag = a.get<std::uint64_t>();
    auto length = a.get<std::uint64_t>();
    auto package = a.get<std::uint32_t>();
    if (tag != 0x29bfca045138de76ull)
        return result;
    if (package != 0x9e2a83c1 || length < 48 || length > a.size())
        throw Error("texture-trailer", "Invalid package trailer footer");
    auto start = a.size() - length;
    a.seek(start);
    if (a.get<std::uint64_t>() != 0xd1c43b2e80a5f697ull)
        throw Error("texture-trailer", "Invalid package trailer header", start);
    auto version = a.get<std::uint32_t>(), header = a.get<std::uint32_t>();
    auto payload_bytes = a.get<std::uint64_t>();
    if (version > 2)
        throw Error("texture-trailer-version", "Unsupported package trailer version", start + 8);
    if (header > length - 20 || payload_bytes != length - 20 - header)
        throw Error("texture-trailer", "Inconsistent trailer lengths", start);
    std::uint8_t legacy = 0;
    if (version == 0)
        legacy = a.get<std::uint8_t>();
    auto count = a.count();
    std::uint64_t stride = version == 0 ? 44 : (version == 1 ? 45 : 49);
    if (header != (version == 0 ? 29u : 28u) + count * stride)
        throw Error("texture-trailer", "Invalid payload index length", a.tell());
    Archive::ReadLimit window(a, start + header);
    std::set<std::string> ids;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto identity = hex_bytes(a, 20);
        auto offset = a.get<std::int64_t>();
        auto stored = a.get<std::uint64_t>(), size = a.get<std::uint64_t>();
        std::uint16_t flags = 0;
        if (version >= 2) {
            flags = a.get<std::uint16_t>();
            a.skip(2);
        }
        auto access = version >= 1 ? a.get<std::uint8_t>() : (offset < 0 ? 2 : legacy);
        if (access > 2 || flags || !ids.insert(identity).second)
            throw Error("texture-trailer", "Invalid payload index entry", a.tell());
        if (access == 0 && (offset < 0 || static_cast<std::uint64_t>(offset) > payload_bytes ||
                            stored > payload_bytes - static_cast<std::uint64_t>(offset)))
            throw Error("texture-trailer", "Payload escapes trailer data", a.tell());
        if (identity != id)
            continue;
        if (size != raw)
            throw Error("texture-trailer", "Texture and trailer payload sizes disagree", a.tell());
        result.raw_bytes = size;
        result.stored_bytes = stored;
        result.storage = access == 0 ? "local" : (access == 1 ? "referenced" : "virtualized");
        if (access == 0) {
            result.file = path;
            result.offset = start + header + static_cast<std::uint64_t>(offset);
        }
    }
    return result;
}
const Json *property(const Json &properties, const char *key) {
    auto it = properties.find(key);
    if (it == properties.end() || !it->contains("value"))
        return nullptr;
    return &it->at("value");
}
std::uint32_t dimension(const Json &props, const char *name, std::uint32_t limit) {
    auto value = property(props, name);
    if (!value)
        return 0;
    if (!value->is_number_integer())
        throw Error("texture-metadata", "Invalid texture dimension");
    auto n = value->get<std::int64_t>();
    if (n < 0 || static_cast<std::uint64_t>(n) > limit)
        throw Error("texture-metadata", "Texture dimension exceeds supported limits");
    return static_cast<std::uint32_t>(n);
}
bool boolean(Archive &a) {
    auto value = a.get<std::uint32_t>();
    if (value > 1)
        throw Error("texture-boolean", "Invalid texture native boolean", a.tell() - 4);
    return value != 0;
}
} // namespace
TexturePayload locate_texture_payload(const fs::path &path, const std::string &id, std::uint64_t size,
                                      const ReadOptions &options) {
    for (const auto *extension : {"", ".uexp", ".upayload"}) {
        auto file = path;
        if (*extension)
            file.replace_extension(extension);
        if (!fs::is_regular_file(file))
            continue;
        Archive a(file, options);
        auto result = trailer(a, file, id, size, options);
        if (result.storage == "unavailable")
            continue;
        if (result.storage == "local") {
            buffer_header(a, result, result.offset + result.stored_bytes);
            if (options.fingerprints)
                result.sha256 = a.hash(result.offset, result.stored_bytes);
        }
        return result;
    }
    TexturePayload result;
    result.content_id = id;
    result.raw_bytes = size;
    return result;
}
EditorBulkResult read_editor_bulk(Archive &a, const Package &package, const ReadOptions &options) {
    auto main = package.summary.custom_versions.value("697dd581e64f41abaa4a51ecbeb7b628", -1);
    if (main < 51 || main > 119)
        throw Error("bulk-profile", "Unsupported editor bulk serialization profile");
    EditorBulkResult result;
    result.flags = a.get<std::uint32_t>();
    if (result.flags & ~0xfffu)
        throw Error("bulk-flags", "Unsupported editor bulk flags", a.tell() - 4);
    result.guid = a.guid();
    auto id = hex_bytes(a, 20);
    auto size = unsigned_size(a);
    std::uint64_t offset = 0;
    if (!(result.flags & (1u | 512u | 1024u)))
        offset = unsigned_size(a);
    auto &payload = result.payload;
    payload.content_id = id;
    payload.raw_bytes = size;
    if (result.flags & 1024u)
        payload.storage = "cooked-unsupported";
    else if (!size)
        payload.storage = "empty";
    else if (result.flags & (4u | 8u | 64u | 128u | 256u | 2048u))
        payload.storage = "legacy-unsupported";
    else if (result.flags & 512u)
        payload = locate_texture_payload(package.source, id, size, options);
    else if (result.flags & 1u)
        payload.storage = "virtualized";
    else {
        payload.file = package.source;
        if (result.flags & 2u)
            payload.file.replace_extension(".upayload");
        payload.offset = offset;
        if (fs::is_regular_file(payload.file)) {
            Archive data(payload.file, options);
            buffer_header(data, payload, data.size());
            payload.storage = "local";
            if (options.fingerprints)
                payload.sha256 = data.hash(payload.offset, payload.stored_bytes);
        }
    }
    return result;
}
bool is_texture2d(const ObjectExport &object) {
    return object.class_name == "/Script/Engine.Texture2D" || object.class_name == "Texture2D";
}
TextureSource texture_metadata(const ObjectExport &object) {
    TextureSource info;
    info.object_path = object.path;
    const auto *source = property(object.properties, "Source");
    Json empty = Json::object();
    const auto &props = source ? *source : empty;
    info.width = dimension(props, "SizeX", 1u << 24);
    info.height = dimension(props, "SizeY", 1u << 24);
    info.mips = dimension(props, "NumMips", 32);
    info.slices = dimension(props, "NumSlices", 2048);
    if (auto value = property(props, "Format"); value && value->is_string())
        info.format = value->get<std::string>();
    if (auto value = property(props, "CompressionFormat"); value && value->is_string())
        info.compression = value->get<std::string>();
    else if (auto png = property(props, "bPNGCompressed"); png && *png == true)
        info.compression = "TSCF_PNG";
    if (auto offsets = property(props, "BlockDataOffsets"); offsets && offsets->is_array()) {
        for (const auto &offset : *offsets) {
            if (!offset.is_number_integer() || offset.get<std::int64_t>() < 0)
                throw Error("texture-metadata", "Invalid source block offset");
            info.block_offsets.push_back(offset.get<std::uint64_t>());
        }
    }
    if (info.block_offsets.empty())
        info.block_offsets.push_back(0);
    if (auto formats = property(props, "LayerFormat"); formats && formats->is_array())
        info.layers = static_cast<std::uint32_t>(formats->size());
    if (!info.layers && source && !props.contains("LayerFormat"))
        info.layers = 1;
    if (auto blocks = property(props, "Blocks"); blocks && (!blocks->is_array() || !blocks->empty())) {
        info.block_offsets.clear();
        info.diagnostics.push_back("Additional or unresolved source blocks require a multi-block decoder.");
    }
    info.settings = object.properties;
    for (const auto *field :
         {"Source", "AssetImportData", "LightingGuid", "ImportedSize", "OodleTextureSdkVersion"})
        info.settings.erase(field);
    return info;
}
TextureDecodeResult decode_texture_source(Archive &a, const Package &package, std::size_t index,
                                          std::uint64_t end, const ReadOptions &options) {
    TextureDecodeResult result;
    result.source = texture_metadata(package.exports[index]);
    auto &info = result.source;
    auto main = package.summary.custom_versions.value("697dd581e64f41abaa4a51ecbeb7b628", -1);
    if (main < 51 || main > 119)
        throw Error("texture-profile", "Unsupported editor texture bulk serialization profile");
    std::uint32_t flags = 0;
    std::uint64_t size = 0, offset = 0;
    std::string content;
    bool cooked = false, stripped = false;
    {
        Archive::ReadLimit window(a, end);
        Json native = {{"kind", "texture-source"}};
        if (boolean(a))
            native["objectGuid"] = a.guid();
        auto global = a.get<std::uint8_t>();
        a.get<std::uint8_t>();
        stripped = (global & 1) != 0;
        if (!stripped) {
            flags = a.get<std::uint32_t>();
            if (flags & ~0xfffu)
                throw Error("texture-flags", "Unsupported editor bulk flags", a.tell() - 4);
            native["bulkGuid"] = a.guid();
            content = hex_bytes(a, 20);
            size = unsigned_size(a);
            if (!(flags & (1u | 512u | 1024u)))
                offset = unsigned_size(a);
        }
        a.skip(2);
        cooked = boolean(a);
        result.end = a.tell();
        native["bulkFlags"] = flags;
        result.native = std::move(native);
    }
    info.payload.content_id = content;
    info.payload.raw_bytes = size;
    if (stripped || cooked || (flags & 1024u)) {
        info.payload.storage = "cooked-unsupported";
        info.diagnostics.push_back("Cooked platform textures are not decoded by the editor source backend.");
    } else if (!size)
        info.payload.storage = "empty";
    else if (flags & (4u | 8u | 256u)) {
        info.payload.storage = "legacy-unsupported";
        info.diagnostics.push_back("Legacy or workspace-domain payload requires another backend.");
    } else if (flags & 512u) {
        info.payload = locate_texture_payload(package.source, content, size, options);
        if (info.payload.storage == "unavailable")
            info.diagnostics.push_back("Source payload not found in available package trailers.");
    } else if (flags & 1u)
        info.payload.storage = "virtualized";
    else {
        TexturePayload payload;
        payload.file = package.source;
        payload.offset = offset;
        payload.raw_bytes = size;
        payload.content_id = content;
        payload.storage = "local";
        Archive file(payload.file, options);
        buffer_header(file, payload, file.size());
        if (options.fingerprints)
            payload.sha256 = file.hash(payload.offset, payload.stored_bytes);
        info.payload = std::move(payload);
    }
    if (info.payload.storage == "local" && info.payload.method != 0)
        info.diagnostics.push_back("Source compressed buffer method " + std::to_string(info.payload.method) +
                                   " requires an explicit block decoder for previews.");
    return result;
}
} // namespace cyuediff::detail
namespace cyuediff {
Json TextureSource::to_json(bool location) const {
    Json content = {
        {"storage", payload.storage}, {"id", payload.content_id}, {"rawBytes", payload.raw_bytes}};
    if (payload.storage == "local") {
        content["bufferMethod"] = payload.method;
        content["storedBytes"] = payload.stored_bytes;
        if (!payload.sha256.empty())
            content["sha256"] = payload.sha256;
    }
    if (location && payload.storage == "local") {
        content["file"] = payload.file.u8string();
        content["offset"] = payload.offset;
    }
    return {{"path", object_path},
            {"width", width},
            {"height", height},
            {"sourceMips", mips},
            {"slices", slices},
            {"layers", layers},
            {"format", format},
            {"compression", compression},
            {"blockOffsets", block_offsets},
            {"settings", settings},
            {"payload", content},
            {"diagnostics", diagnostics}};
}
Json Package::texture_snapshot() const {
    Json entries = Json::object();
    for (const auto &texture : textures)
        entries[texture.object_path] = texture.to_json();
    return {{"schema", "cyuediff.textures/1"}, {"textures", std::move(entries)}};
}
} // namespace cyuediff

namespace cyuediff {
std::vector<std::uint8_t> decode_bulk_payload(const BulkPayload &p, const BulkDecodeOptions &options) {
    if (p.storage != "local")
        throw Error("texture-payload", "Bulk source is not locally readable: " + p.storage);
    if (p.stored_bytes < 64 || p.stored_bytes > options.max_payload_bytes ||
        p.raw_bytes > options.max_payload_bytes || p.stored_bytes > SIZE_MAX || p.raw_bytes > SIZE_MAX)
        throw Error("limit", "Bulk source exceeds configured payload limit");
    ReadOptions read;
    read.control = options.control;
    options.control.checkpoint("Reading bulk data", 0, p.raw_bytes);
    detail::Archive a(p.file, read);
    if (p.offset > a.size() || p.stored_bytes > a.size() - p.offset)
        throw Error("bounds", "Bulk payload escapes input file");
    auto checked = p;
    detail::buffer_header(a, checked, p.offset + p.stored_bytes);
    a.seek(p.offset);
    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(p.stored_bytes));
    a.read(encoded.data(), encoded.size());
    if (!p.sha256.empty() && sha256_bytes(encoded, options.control) != p.sha256)
        throw Error("source-changed", "Bulk source changed while reading");
    if (checked.method == 0) {
        encoded.erase(encoded.begin(), encoded.begin() + 64);
        return encoded;
    }
    auto big = [&](std::size_t at, unsigned count) {
        std::uint64_t v = 0;
        for (unsigned i = 0; i < count; ++i)
            v = (v << 8) | encoded.at(at + i);
        return v;
    };
    auto blocks = big(12, 4);
    auto exponent = encoded[11];
    if (exponent > 31 || !p.raw_bytes || blocks != 1 + (p.raw_bytes - 1) / (1ull << exponent))
        throw Error("bulk-layout", "Invalid compressed block layout");
    std::size_t position = 64 + static_cast<std::size_t>(blocks) * 4;
    std::uint64_t remaining = p.raw_bytes;
    for (std::size_t i = 0; i < blocks; ++i) {
        auto size = big(64 + i * 4, 4), raw = std::min<std::uint64_t>(remaining, 1ull << exponent);
        if (!size || size > raw || size > encoded.size() - position)
            throw Error("bulk-layout", "Invalid compressed block size");
        position += static_cast<std::size_t>(size);
        remaining -= raw;
    }
    if (position != encoded.size() || remaining)
        throw Error("bulk-layout", "Compressed blocks do not cover payload");
    if (!options.decoder)
        throw Error("texture-codec",
                    "Bulk source requires a decoder for method " + std::to_string(checked.method));
    std::vector<std::uint8_t> output(static_cast<std::size_t>(p.raw_bytes));
    position = 64 + static_cast<std::size_t>(blocks) * 4;
    std::size_t destination = 0;
    for (std::size_t i = 0; i < blocks; ++i) {
        options.control.checkpoint("Decoding bulk data", destination, p.raw_bytes);
        auto size = static_cast<std::size_t>(big(64 + i * 4, 4));
        auto raw = std::min<std::size_t>(output.size() - destination, std::size_t(1) << exponent);
        if (size == raw)
            std::copy_n(encoded.data() + position, raw, output.data() + destination);
        else if (options.decoder(checked.method, encoded.data() + position, size, output.data() + destination,
                                 raw) != raw)
            throw Error("bulk-codec", "Block decoder did not produce the expected byte count");
        position += size;
        destination += raw;
        options.control.check();
    }
    options.control.checkpoint("Decoding bulk data", p.raw_bytes, p.raw_bytes);
    return output;
}
} // namespace cyuediff
