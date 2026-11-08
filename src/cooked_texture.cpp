#include <cyuediff/cooked_texture.hpp>
#include "archive.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace cyuediff {
namespace {
void need(bool value, const char *message, std::uint64_t offset = 0) {
    if (!value)
        throw Error("cooked-texture-layout", message, offset);
}
struct Cursor {
    const std::vector<std::uint8_t> &bytes;
    std::uint64_t pos, end;
    void range(std::uint64_t n) const {
        need(pos <= end && n <= end - pos, "Cooked texture field exceeds its native window", pos);
    }
    std::uint64_t number(unsigned n) {
        range(n);
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i)
            v |= std::uint64_t(bytes[std::size_t(pos++)]) << (i * 8);
        return v;
    }
    std::uint32_t u32() {
        return static_cast<std::uint32_t>(number(4));
    }
    bool boolean() {
        auto v = u32();
        need(v <= 1, "Invalid cooked texture bool32", pos - 4);
        return v != 0;
    }
    void zeros(unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            need(number(1) == 0, "Nonzero cooked texture reserved bytes", pos - 1);
    }
    std::string name(const std::vector<std::string> &names) {
        const auto i = u32(), n = u32();
        need(i < names.size() && n == 0, "Invalid cooked pixel-format FName", pos - 8);
        return names[i];
    }
    std::string format_string() {
        auto n = u32();
        need(n >= 2 && n <= 64, "Invalid cooked pixel-format string size", pos - 4);
        range(n);
        std::string out;
        for (unsigned i = 0; i < n - 1; ++i) {
            auto ch = number(1);
            need(ch > 0 && ch < 128, "Invalid cooked pixel-format text", pos - 1);
            out.push_back(static_cast<char>(ch));
        }
        need(number(1) == 0, "Unterminated cooked pixel-format string", pos - 1);
        return out;
    }
};
OperationControl controls(const CookedTextureOptions &options, const ZenReadOptions &zen,
                          const ContainerReadOptions &read) {
    OperationControl out;
    out.cancelled = [a = options.control.cancelled, b = zen.control.cancelled, c = read.control.cancelled] {
        return (a && a()) || (b && b()) || (c && c());
    };
    out.progress = options.control.progress
                       ? options.control.progress
                       : (zen.control.progress ? zen.control.progress : read.control.progress);
    return out;
}
std::size_t find_chunk(const Container &container, const std::string &id) {
    std::size_t found = SIZE_MAX;
    for (std::size_t i = 0; i < container.entries().size(); ++i) {
        const auto &e = container.entries()[i];
        if (e.id == id && !e.deleted) {
            need(found == SIZE_MAX, "Ambiguous cooked texture bulk chunk");
            found = i;
        }
    }
    return found;
}
void payload(CookedTextureMip &mip, const std::vector<std::uint8_t> &bytes, bool load,
             const OperationControl &control) {
    need(mip.offset <= bytes.size() && mip.size <= bytes.size() - mip.offset,
         "Cooked mip range exceeds verified chunk", mip.offset);
    mip.sha256 = detail::hash_range(bytes, mip.offset, mip.size, control);
    if (load) {
        mip.encoded.assign(bytes.begin() + static_cast<std::ptrdiff_t>(mip.offset),
                           bytes.begin() + static_cast<std::ptrdiff_t>(mip.offset + mip.size));
        mip.loaded = true;
    }
}
} // namespace
Json CookedTextureMip::to_json() const {
    return {{"width", width},          {"height", height},    {"bulkIndex", bulk_index},
            {"bulkFlags", bulk_flags}, {"offset", offset},    {"size", size},
            {"storage", storage},      {"chunkId", chunk_id}, {"chunkSourceHash", chunk_source_hash},
            {"sha256", sha256},        {"loaded", loaded},    {"unavailable", unavailable}};
}
Json CookedTexturePlatform::to_json() const {
    Json list = Json::array();
    for (const auto &m : mips)
        list.push_back(m.to_json());
    return {{"format", format},
            {"originalWidth", original_width},
            {"originalHeight", original_height},
            {"firstMip", first_mip},
            {"mips", std::move(list)}};
}
Json CookedTexture2D::to_json() const {
    Json list = Json::array();
    for (const auto &p : platforms)
        list.push_back(p.to_json());
    return {{"schema", "cyuediff.cooked-texture/1"},
            {"object", object_path},
            {"profile", profile},
            {"packageSha256", package_sha256},
            {"schemaContextSha256", schema_context_sha256},
            {"mappingVerified", false},
            {"exportIndex", export_index},
            {"nativeOffset", native_offset},
            {"nativeBytes", native_bytes},
            {"platforms", std::move(list)},
            {"samples", "stored-channels-without-color-transform"}};
}
struct CookedTextureReader::Impl {
    Container container;
    std::size_t entry;
    ZenPackage package;
    std::vector<std::uint8_t> bytes;
    ContainerReadOptions reads;
    CookedTextureOptions options;
    OperationControl control;
    std::uint64_t total_read = 0;
    std::map<std::string, std::vector<std::uint8_t>> chunks;
    Impl(const Container &c, std::size_t e) : container(c), entry(e) {}
};
CookedTextureReader::CookedTextureReader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CookedTextureReader::~CookedTextureReader() = default;
CookedTextureReader::CookedTextureReader(CookedTextureReader &&) noexcept = default;
CookedTextureReader &CookedTextureReader::operator=(CookedTextureReader &&) noexcept = default;
const ZenPackage &CookedTextureReader::package() const {
    need(impl_ != nullptr, "Cooked texture reader has no session");
    return impl_->package;
}
CookedTextureReader CookedTextureReader::open(const Container &container, std::size_t entry,
                                              const ZenReadOptions &zen_options,
                                              const ContainerReadOptions &read_options,
                                              const CookedTextureOptions &options) {
    auto impl = std::make_unique<Impl>(container, entry);
    impl->control = controls(options, zen_options, read_options);
    impl->control.check();
    if (zen_options.profile != ZenProfile::unreal_5_3)
        throw Error("cooked-texture-profile",
                    "Cooked Texture2D native pixels require the explicit UE 5.3 profile");
    need(entry < container.entries().size(), "Invalid cooked texture package entry");
    const auto &member = container.entries()[entry];
    need(member.chunk_type == 1 && member.id.size() == 24,
         "Cooked texture requires a Zen export-bundle chunk");
    if (member.size > options.max_read_bytes)
        throw Error("cooked-texture-limit", "Cooked texture package exceeds read budget");
    impl->options = options;
    impl->reads = read_options;
    impl->reads.control = impl->control;
    impl->bytes = container.read(entry, impl->reads);
    impl->total_read = impl->bytes.size();
    auto zopts = zen_options;
    zopts.control = impl->control;
    impl->package = ZenPackage::parse(impl->bytes, zopts);
    if ((impl->package.flags & 0x80000200u) != 0x80000200u)
        throw Error("cooked-texture-profile", "Cooked Texture2D requires an editor-stripped cooked package");
    return CookedTextureReader(std::move(impl));
}
CookedTexture2D CookedTextureReader::read(std::size_t selected) {
    need(impl_ != nullptr, "Cooked texture reader has no session");
    const auto &container = impl_->container;
    const auto &member = container.entries()[impl_->entry];
    const auto &options = impl_->options;
    const auto &package = impl_->package;
    const auto &control = impl_->control;
    const auto &bytes = impl_->bytes;
    control.check();
    const auto &reads = impl_->reads;
    if (selected == SIZE_MAX) {
        for (std::size_t i = 0; i < package.exports.size(); ++i)
            if (package.exports[i].class_name == "/Script/Engine.Texture2D" &&
                !(package.exports[i].flags & 0x30)) {
                need(selected == SIZE_MAX, "Select one of several cooked Texture2D exports");
                selected = i;
            }
    }
    need(selected < package.exports.size(), "No selected cooked Texture2D export");
    const auto &object = package.exports[selected];
    need(object.class_name == "/Script/Engine.Texture2D" && !(object.flags & 0x30),
         "Selected export is not a non-template Texture2D");
    if (object.property_read.is_null() || object.property_read.value("status", std::string{}) != "decoded")
        throw Error("cooked-texture-context",
                    "Cooked Texture2D requires a successfully decoded explicit property binding");
    auto prop_offset = object.property_read.at("offset").get<std::uint64_t>();
    auto prop_bytes = object.property_read.at("consumedBytes").get<std::uint64_t>();
    need(prop_offset <= object.size && prop_bytes <= object.size - prop_offset,
         "Invalid cooked property boundary");
    CookedTexture2D result;
    result.export_index = selected;
    result.object_path = object.path;
    result.profile = package.profile;
    result.package_sha256 = package.sha256;
    result.schema_context_sha256 = package.schema_context_sha256;
    result.native_offset = object.offset + prop_offset + prop_bytes;
    result.native_bytes = object.offset + object.size - result.native_offset;
    Cursor r{bytes, result.native_offset, object.offset + object.size};
    if (r.boolean()) {
        r.range(16);
        r.pos += 16;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const auto global = r.number(1), classes = r.number(1);
        need((global & 1) && !(global & ~3ull) && classes == 0, "Unsupported cooked texture strip flags",
             r.pos - 2);
    }
    need(r.boolean(), "Texture2D native data is not cooked", r.pos - 4);
    if (!r.boolean())
        throw Error("cooked-texture-profile", "Custom mip-provider Texture2D has no serialized mip payload");
    std::uint64_t total_payload = 0;
    std::set<std::string> formats;
    for (;;) {
        control.checkpoint("cooked-texture-layout", result.platforms.size(), 0);
        auto format = r.name(package.names);
        if (format == "None")
            break;
        if (result.platforms.size() >= options.max_formats)
            throw Error("cooked-texture-limit", "Too many cooked texture pixel formats");
        need(formats.insert(format).second, "Duplicate cooked texture pixel format");
        const auto pixel_format = unreal_gpu_pixel_format(format);
        const auto skip_at = r.pos, skip = r.number(8);
        need(skip >= 8 && skip <= r.end - skip_at, "Invalid cooked texture format skip offset", skip_at);
        const auto format_end = skip_at + skip, export_end = r.end;
        r.end = format_end;
        if (r.number(1) != 0)
            throw Error("cooked-texture-profile",
                        "Cooked texture derived-data references require another native profile");
        r.zeros(15);
        CookedTexturePlatform platform;
        platform.format = format;
        platform.original_width = r.u32();
        platform.original_height = r.u32();
        const auto packed = r.u32();
        need(platform.original_width && platform.original_height &&
                 platform.original_width <= options.max_dimension &&
                 platform.original_height <= options.max_dimension,
             "Invalid cooked texture dimensions");
        if ((packed & 0x80000000u) || (packed & 0x3fffffffu) != 1)
            throw Error("cooked-texture-profile",
                        "Cooked pixel reader requires one 2D slice without cubemap packing");
        need(r.format_string() == format, "Cooked texture format name disagrees with platform header");
        if (packed & 0x40000000u) {
            const auto extra = r.u32(), tail = r.u32();
            if (extra || tail)
                throw Error("cooked-texture-profile",
                            "Platform-specific mip tails require another native profile");
        }
        platform.first_mip = r.u32();
        const auto count = r.u32();
        need(platform.first_mip < 32 && count && count <= 32 - platform.first_mip,
             "Invalid cooked mip count or first mip");
        if (count > options.max_mips)
            throw Error("cooked-texture-limit", "Cooked mip count exceeds configured limit");
        std::uint32_t width = std::max(1u, platform.original_width >> platform.first_mip),
                      height = std::max(1u, platform.original_height >> platform.first_mip);
        for (unsigned i = 0; i < count; ++i) {
            control.checkpoint("cooked-texture-mip", i, count);
            if (package.bulk_data.empty())
                throw Error("cooked-texture-profile",
                            "Legacy inline bulk metadata requires another native profile");
            CookedTextureMip mip;
            mip.bulk_index = r.u32();
            need(mip.bulk_index < package.bulk_data.size(), "Cooked mip bulk-map index exceeds table",
                 r.pos - 4);
            const auto &bulk = package.bulk_data[mip.bulk_index];
            mip.bulk_flags = bulk.at("flags").get<std::uint32_t>();
            mip.size = bulk.at("size").get<std::uint64_t>();
            if (mip.bulk_flags & ~0x1ffffu || (mip.bulk_flags & ((1u << 1) | (1u << 9) | (1u << 15))))
                throw Error("cooked-texture-profile",
                            "Unsupported cooked mip bulk flags or legacy bulk compression");
            if (mip.size > options.max_payload_bytes - total_payload)
                throw Error("cooked-texture-limit", "Cooked mip payloads exceed aggregate byte budget");
            total_payload += mip.size;
            if (mip.bulk_flags & (1u << 5))
                mip.unavailable = "unused";
            else if (!(mip.bulk_flags & 1)) {
                need(!(mip.bulk_flags & ((1u << 8) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 14))),
                     "Conflicting inline cooked mip bulk flags");
                mip.storage = "inline";
                mip.offset = r.pos;
                mip.chunk_id = member.id;
                mip.chunk_source_hash = member.source_hash;
                r.range(mip.size);
                r.pos += mip.size;
            } else {
                mip.storage = "external";
                const auto offset = bulk.at("offset").get<std::int64_t>();
                need(offset >= 0, "Negative external cooked mip offset");
                mip.offset = static_cast<std::uint64_t>(offset);
                unsigned type = mip.bulk_flags & (1u << 11) ? 3 : 2;
                if (mip.bulk_flags & (1u << 14)) {
                    auto optional = member.id;
                    optional.replace(22, 2, "03");
                    if (find_chunk(container, optional) != SIZE_MAX) {
                        type = 3;
                        const auto duplicate = bulk.at("duplicateOffset").get<std::int64_t>();
                        need(duplicate >= 0, "Negative duplicate cooked mip offset");
                        mip.offset = static_cast<std::uint64_t>(duplicate);
                    }
                } else if (mip.bulk_flags & (1u << 12))
                    type = 4;
                mip.chunk_id = member.id;
                mip.chunk_id.replace(22, 2, type == 2 ? "02" : type == 3 ? "03" : "04");
            }
            mip.width = r.u32();
            mip.height = r.u32();
            const auto depth = r.u32();
            need(mip.width == width && mip.height == height && depth == 1,
                 "Cooked mip dimensions disagree with 2D chain");
            if (mip.unavailable.empty())
                need(mip.size == gpu_texture_bytes(pixel_format, width, height),
                     "Cooked mip byte count disagrees with pixel format");
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
            platform.mips.push_back(std::move(mip));
        }
        if (r.boolean())
            throw Error("cooked-texture-profile",
                        "Virtual cooked texture tiles require another native reader");
        need(r.pos == format_end, "Cooked texture format does not end at its declared boundary", r.pos);
        r.end = export_end;
        result.platforms.push_back(std::move(platform));
    }
    need(r.pos == r.end && !result.platforms.empty(),
         "Trailing native texture bytes or absent cooked platform data", r.pos);
    // No payload is published before the complete native structure has been validated.
    auto &total_read = impl_->total_read;
    auto &chunks = impl_->chunks;
    for (auto &platform : result.platforms)
        for (auto &mip : platform.mips) {
            control.check();
            if (!mip.unavailable.empty())
                continue;
            if (mip.storage == "inline")
                payload(mip, bytes, options.load_payloads, control);
            else {
                const auto found = find_chunk(container, mip.chunk_id);
                if (found == SIZE_MAX) {
                    mip.unavailable = "missing-chunk";
                    continue;
                }
                const auto &bulk = container.entries()[found];
                mip.chunk_source_hash = bulk.source_hash;
                need(mip.offset <= bulk.size && mip.size <= bulk.size - mip.offset,
                     "Cooked mip range exceeds external chunk", mip.offset);
                if (!options.load_payloads)
                    continue;
                auto it = chunks.find(mip.chunk_id);
                if (it == chunks.end()) {
                    if (bulk.size > options.max_read_bytes - total_read)
                        throw Error("cooked-texture-limit",
                                    "Verified cooked texture chunks exceed aggregate read budget");
                    total_read += bulk.size;
                    it = chunks.emplace(mip.chunk_id, container.read(found, reads)).first;
                }
                payload(mip, it->second, true, control);
            }
        }
    control.check();
    return result;
}
CookedTexture2D read_zen_texture2d(const Container &container, std::size_t entry, const ZenReadOptions &zen,
                                   const ContainerReadOptions &read, const CookedTextureOptions &options,
                                   std::size_t selected) {
    return CookedTextureReader::open(container, entry, zen, read, options).read(selected);
}
ImageRgba8 decode_cooked_mip(const CookedTexturePlatform &platform, std::size_t index,
                             const GpuDecodeOptions &options) {
    if (index >= platform.mips.size() || !platform.mips[index].loaded)
        throw Error("cooked-texture-unavailable", "Selected cooked mip payload is not loaded");
    const auto &m = platform.mips[index];
    return decode_gpu_rgba8(m.encoded, m.width, m.height, unreal_gpu_pixel_format(platform.format), options);
}
ImageRgba32f decode_cooked_mip_hdr(const CookedTexturePlatform &platform, std::size_t index,
                                   const GpuDecodeOptions &options) {
    if (index >= platform.mips.size() || !platform.mips[index].loaded)
        throw Error("cooked-texture-unavailable", "Selected cooked mip payload is not loaded");
    const auto &m = platform.mips[index];
    return decode_gpu_rgba32f(m.encoded, m.width, m.height, unreal_gpu_pixel_format(platform.format),
                              options);
}
} // namespace cyuediff
