#include "native.hpp"
#include "string_value.hpp"
#include <cyuediff/string_encoding.hpp>
#include <set>
namespace cyuediff::detail {
namespace {
struct MetadataReader {
    Archive &a;
    const Package &p;
    const ReadOptions &o;
    std::uint64_t end;
    std::uint32_t work = 0;
    std::uint64_t text = 0;
    void tick() {
        o.control.check();
        if (work++ >= o.max_properties)
            throw Error("limit", "Package metadata work limit exceeded", a.tell());
    }
    void spend(std::uint64_t n) {
        if (n > o.max_property_bytes - text)
            throw Error("limit", "Package metadata text limit exceeded", a.tell());
        text += n;
    }
    std::uint64_t encoded(const std::string &s) {
        std::uint64_t n = 2;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (!(i & 4095))
                o.control.check();
            const auto c = static_cast<unsigned char>(s[i]);
            n += c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t' ? 2
                 : c < 32                                                                               ? 6
                                                                                                        : 1;
        }
        return n;
    }
    std::string reference(std::int32_t index) {
        std::uint64_t size = 0;
        auto at = index;
        unsigned depth = 0;
        while (at) {
            o.control.check();
            if (++depth > 128)
                throw Error("metadata-reference", "Metadata reference outer chain exceeds its limit",
                            a.tell());
            const std::string *name = nullptr;
            if (at > 0) {
                const auto n = static_cast<std::size_t>(at - 1);
                if (n >= p.exports.size())
                    throw Error("metadata-reference", "Invalid metadata export reference", a.tell());
                name = &p.exports[n].name;
                at = p.exports[n].outer_index;
            } else {
                const auto n = static_cast<std::uint64_t>(-std::int64_t(at) - 1);
                if (n >= p.imports.size())
                    throw Error("metadata-reference", "Invalid metadata import reference", a.tell());
                name = &p.imports[static_cast<std::size_t>(n)].name;
                at = p.imports[static_cast<std::size_t>(n)].outer_index;
            }
            if (name->size() + 1 > o.max_property_bytes - size)
                throw Error("limit", "Metadata reference text exceeds its limit", a.tell());
            size += name->size() + 1;
        }
        spend(size);
        auto value = p.reference(index);
        validate_byte_string(value, false, o.control, "metadata-string", a.tell());
        return value;
    }
    std::string string() {
        tick();
        const auto start = a.tell();
        const auto length = a.get<std::int32_t>();
        a.seek(start);
        const auto units = length < 0 ? -std::int64_t(length) : std::int64_t(length);
        if (static_cast<std::uint64_t>(units) > o.max_string_bytes / (length < 0 ? 2 : 1) ||
            (units && static_cast<std::uint64_t>(units - 1) > o.max_property_bytes - text))
            throw Error("limit", "Metadata FString exceeds its limit", start);
        auto value = a.string();
        if (length >= 0 && o.narrow_string_encoding == NarrowStringEncoding::unspecified)
            validate_byte_string(value, true, o.control, "metadata-string-encoding", start);
        else
            validate_byte_string(value, false, o.control, "metadata-string", start);
        spend(value.size());
        return value;
    }
    Json values() {
        tick();
        const auto count = a.count(o.max_properties);
        if (count > (end - a.tell()) / 12)
            throw Error("metadata-count", "Metadata names exceed their native window", a.tell() - 4);
        Json result = Json::object();
        for (std::uint32_t i = 0; i < count; ++i) {
            tick();
            const auto index = a.get<std::int32_t>(), number = a.get<std::int32_t>();
            if (index < 0 || static_cast<std::size_t>(index) >= p.names.size() || number < 0)
                throw Error("metadata-name", "Invalid metadata FName", a.tell() - 8);
            const auto &base = p.names[static_cast<std::size_t>(index)];
            validate_byte_string(base, false, o.control, "metadata-name", a.tell() - 8);
            spend(base.size());
            spend(encoded(base) + std::to_string(number).size() + 3);
            const auto id = Json::array({base, number}).dump();
            if (result.contains(id))
                throw Error("metadata-key", "Duplicate stored metadata name", a.tell() - 8);
            auto value = string();
            result[id] = {{"name", {{"base", base}, {"number", number}}}, {"value", std::move(value)}};
        }
        return result;
    }
};
} // namespace
bool is_package_metadata(const ObjectExport &object) {
    return object.class_name == "/Script/CoreUObject.MetaData";
}
NativeDecodeResult decode_package_metadata(Archive &a, const Package &p, std::uint64_t end,
                                           const ReadOptions &o) {
    const auto &s = p.summary;
    const auto editor = s.custom_versions.value("e4b068edf49442e9a231da0b2e46bb41", -1);
    if (s.ue4_version != 522 || (s.ue5_version != 1009 && s.ue5_version != 1013) || s.licensee_version != 0 ||
        editor < 3 || editor > 40 || (s.flags & 0x80002000u))
        throw Error("metadata-profile", "Unsupported source package metadata profile", a.tell());
    if (end < a.tell() || end - a.tell() > o.max_property_bytes)
        throw Error("limit", "Package metadata native window exceeds its limit", a.tell());
    Archive::ReadLimit window(a, end);
    MetadataReader reader{a, p, o, end};
    const auto has_guid = a.get<std::uint32_t>();
    if (has_guid > 1)
        throw Error("metadata-guid", "Invalid metadata UObject GUID marker", a.tell() - 4);
    Json result = {{"kind", "package-metadata"}, {"scope", "stored"}};
    if (has_guid) {
        auto guid = a.guid();
        if (guid == std::string(32, '0'))
            throw Error("metadata-guid", "Zero declared metadata UObject GUID", a.tell() - 16);
        result["objectGuid"] = std::move(guid);
    }
    reader.tick();
    const auto count = a.count(o.max_properties);
    if (count > (end - a.tell()) / 8)
        throw Error("metadata-count", "Metadata objects exceed their native window", a.tell() - 4);
    Json objects = Json::object();
    for (std::uint32_t i = 0; i < count; ++i) {
        reader.tick();
        const auto index = a.get<std::int32_t>();
        Json object = index ? Json(reader.reference(index)) : Json(nullptr);
        if (index)
            reader.spend(reader.encoded(object.get_ref<const std::string &>()));
        else
            reader.spend(4);
        const auto id = object.dump();
        if (objects.contains(id))
            throw Error("metadata-key", "Duplicate metadata object reference", a.tell() - 4);
        objects[id] = {{"object", std::move(object)}, {"values", reader.values()}};
    }
    result["objectValues"] = std::move(objects);
    result["rootValues"] = reader.values();
    if (a.tell() != end)
        throw Error("metadata-size", "Trailing bytes after package metadata", a.tell());
    return {std::move(result), a.tell()};
}
} // namespace cyuediff::detail
