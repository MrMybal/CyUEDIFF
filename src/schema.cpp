#include <cyuediff/schema.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
namespace cyuediff {
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw Error("schema-format", message);
}
struct Reader {
    const std::vector<std::uint8_t> &data;
    const OperationControl &control;
    std::uint64_t pos, end;
    void range(std::uint64_t n) const {
        control.check();
        if (pos > end || n > end - pos)
            throw Error("bounds", "Schema/property read exceeds its window", pos);
    }
    std::uint64_t number(unsigned bytes) {
        range(bytes);
        std::uint64_t n = 0;
        for (unsigned j = 0; j < bytes; ++j)
            n |= std::uint64_t(data[static_cast<std::size_t>(pos++)]) << (8 * j);
        return n;
    }
    std::int32_t i32() {
        return static_cast<std::int32_t>(number(4));
    }
    std::uint32_t count(std::uint32_t limit) {
        auto n = number(4);
        if (n > limit)
            throw Error("schema-limit", "Schema/property count exceeds configured limit", pos - 4);
        return static_cast<std::uint32_t>(n);
    }
    void skip(std::uint64_t n) {
        range(n);
        pos += n;
    }
};
const char *type_name(unsigned kind) {
    static const char *names[] = {"ByteProperty",        "BoolProperty",
                                  "IntProperty",         "FloatProperty",
                                  "ObjectProperty",      "NameProperty",
                                  "DelegateProperty",    "DoubleProperty",
                                  "ArrayProperty",       "StructProperty",
                                  "StrProperty",         "TextProperty",
                                  "InterfaceProperty",   "MulticastDelegateProperty",
                                  "WeakObjectProperty",  "LazyObjectProperty",
                                  "AssetObjectProperty", "SoftObjectProperty",
                                  "UInt64Property",      "UInt32Property",
                                  "UInt16Property",      "Int64Property",
                                  "Int16Property",       "Int8Property",
                                  "MapProperty",         "SetProperty",
                                  "EnumProperty",        "FieldPathProperty",
                                  "OptionalProperty",    "Utf8StrProperty",
                                  "AnsiStrProperty"};
    return kind < 31 ? names[kind] : "Unknown";
}
void append_utf8(std::string &s, std::uint32_t cp) {
    if (cp < 128)
        s += static_cast<char>(cp);
    else if (cp < 2048) {
        s += static_cast<char>(192 | (cp >> 6));
        s += static_cast<char>(128 | (cp & 63));
    } else if (cp < 65536) {
        s += static_cast<char>(224 | (cp >> 12));
        s += static_cast<char>(128 | ((cp >> 6) & 63));
        s += static_cast<char>(128 | (cp & 63));
    } else {
        s += static_cast<char>(240 | (cp >> 18));
        s += static_cast<char>(128 | ((cp >> 12) & 63));
        s += static_cast<char>(128 | ((cp >> 6) & 63));
        s += static_cast<char>(128 | (cp & 63));
    }
}
} // namespace
struct UnversionedSchemas::Impl {
    std::string hash;
    unsigned version = 0;
    Json versioning = nullptr, enums = Json::object();
    std::map<std::string, SchemaDefinition> schemas;
};
UnversionedSchemas::UnversionedSchemas(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
UnversionedSchemas UnversionedSchemas::read(const fs::path &path, const SchemaReadOptions &options) {
    options.control.check();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw Error("file-open", "Cannot open USMAP mappings: " + path.u8string());
    auto size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > options.max_bytes)
        throw Error("schema-limit", "USMAP file exceeds configured limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    for (std::size_t at = 0; at < bytes.size();) {
        options.control.check();
        auto n = std::min<std::size_t>(65536, bytes.size() - at);
        if (!file.read(reinterpret_cast<char *>(bytes.data() + at), n))
            throw Error("file-read", "Cannot read USMAP mappings");
        at += n;
    }
    return parse(bytes, options);
}
UnversionedSchemas UnversionedSchemas::parse(const std::vector<std::uint8_t> &bytes,
                                             const SchemaReadOptions &options) {
    if (bytes.size() > options.max_bytes)
        throw Error("schema-limit", "USMAP file exceeds configured limit");
    Reader r{bytes, options.control, 0, bytes.size()};
    require(r.number(2) == 0x30c4, "Invalid USMAP magic");
    auto p = std::make_shared<Impl>();
    p->version = static_cast<unsigned>(r.number(1));
    require(p->version <= 4, "Unsupported USMAP version");
    if (p->version >= 1) {
        auto has = r.number(4);
        require(has <= 1, "Invalid USMAP versioning flag");
        if (has) {
            auto ue4 = r.i32(), ue5 = r.i32();
            auto count = r.count(options.max_entries);
            Json custom = Json::array();
            r.range(std::uint64_t(count) * 20);
            for (unsigned j = 0; j < count; ++j) {
                std::string guid;
                static const char digits[] = "0123456789abcdef";
                for (unsigned k = 0; k < 16; ++k) {
                    auto b = r.number(1);
                    guid += digits[b >> 4];
                    guid += digits[b & 15];
                }
                custom.push_back({{"guidBytes", guid}, {"version", r.i32()}});
            }
            p->versioning = {{"ue4", ue4}, {"ue5", ue5}, {"customVersions", custom}, {"netCL", r.number(4)}};
        }
    }
    auto compression = r.number(1), compressed = r.number(4), expanded = r.number(4);
    require(compressed == r.end - r.pos, "USMAP compressed size mismatch");
    if (expanded > options.max_bytes)
        throw Error("schema-limit", "Expanded USMAP exceeds configured limit");
    std::vector<std::uint8_t> payload;
    if (!compression) {
        require(compressed == expanded, "USMAP uncompressed size mismatch");
        payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(r.pos), bytes.end());
    } else {
        require(compression <= 3, "Unsupported USMAP compression method");
        if (!options.decoder)
            throw Error("schema-codec", "Compressed USMAP requires an explicit decoder");
        payload.resize(static_cast<std::size_t>(expanded));
        const char *method = compression == 1 ? "Oodle" : (compression == 2 ? "Brotli" : "Zstd");
        options.control.check();
        auto written = options.decoder(method, bytes.data() + r.pos, static_cast<std::size_t>(compressed),
                                       payload.data(), payload.size());
        options.control.check();
        require(written == payload.size(), "USMAP decoder output size mismatch");
    }
    Reader in{payload, options.control, 0, payload.size()};
    std::vector<std::string> names;
    auto count = in.count(options.max_entries);
    for (unsigned j = 0; j < count; ++j) {
        auto n = in.number(p->version >= 2 ? 2 : 1);
        in.range(n);
        std::string name;
        for (unsigned k = 0; k < n; ++k) {
            auto c = in.number(1);
            require(c > 0 && c < 128, "USMAP names must contain non-NUL ASCII bytes");
            name += static_cast<char>(c);
        }
        names.push_back(std::move(name));
    }
    auto name = [&](bool optional = false) {
        auto index = in.i32();
        if (optional && index == -1)
            return std::string{};
        require(index >= 0 && static_cast<std::size_t>(index) < names.size(), "Invalid USMAP name index");
        auto value = names[static_cast<std::size_t>(index)];
        require(!value.empty(), "Empty USMAP identity");
        return value;
    };
    count = in.count(options.max_entries);
    std::uint64_t entries = 0;
    auto charge = [&](std::uint64_t n) {
        if (n > options.max_entries - entries)
            throw Error("schema-limit", "USMAP total descriptor count exceeds limit");
        entries += n;
    };
    for (unsigned j = 0; j < count; ++j) {
        auto key = name();
        require(!p->enums.contains(key), "Duplicate USMAP enum name");
        auto n = in.number(p->version >= 3 ? 2 : 1);
        charge(n);
        Json values = Json::array();
        for (unsigned k = 0; k < n; ++k) {
            auto value =
                p->version >= 4 ? static_cast<std::int64_t>(in.number(8)) : static_cast<std::int64_t>(k);
            values.push_back({{"value", value}, {"name", name()}});
        }
        p->enums[key] = std::move(values);
    }
    std::function<SchemaType(unsigned)> type = [&](unsigned depth) {
        if (depth >= options.max_depth)
            throw Error("schema-limit", "USMAP type nesting exceeds limit");
        charge(1);
        SchemaType t;
        t.kind = static_cast<std::uint8_t>(in.number(1));
        require(t.kind <= 30 || t.kind == 255, "Unsupported USMAP type descriptor");
        if (t.kind == 26) {
            t.arguments.push_back(type(depth + 1));
            t.name = name();
        } else if (t.kind == 9)
            t.name = name();
        else if (t.kind == 8 || t.kind == 25 || t.kind == 28)
            t.arguments.push_back(type(depth + 1));
        else if (t.kind == 24) {
            t.arguments.push_back(type(depth + 1));
            t.arguments.push_back(type(depth + 1));
        }
        return t;
    };
    count = in.count(options.max_entries);
    for (unsigned j = 0; j < count; ++j) {
        SchemaDefinition schema;
        schema.name = name();
        schema.super = name(true);
        schema.slots = static_cast<std::uint16_t>(in.number(2));
        charge(schema.slots);
        auto descriptors = in.number(2);
        require(descriptors <= schema.slots, "Too many USMAP properties for schema slots");
        std::set<std::string> property_names;
        for (unsigned k = 0; k < descriptors; ++k) {
            SchemaProperty property;
            property.index = static_cast<std::uint16_t>(in.number(2));
            property.array_size = static_cast<std::uint8_t>(in.number(1));
            property.name = name();
            property.type = type(0);
            require(property.array_size && unsigned(property.index) + property.array_size <= schema.slots,
                    "Invalid USMAP static array range");
            require(property_names.insert(property.name).second, "Duplicate USMAP property name");
            for (unsigned n = 0; n < property.array_size; ++n)
                require(schema.properties.emplace(static_cast<std::uint16_t>(property.index + n), property)
                            .second,
                        "Overlapping USMAP property slots");
        }
        require(p->schemas.emplace(schema.name, std::move(schema)).second, "Duplicate USMAP schema identity");
        options.control.checkpoint("schema-load", j + 1, count);
    }
    if (in.pos != in.end)
        throw Error("schema-extension",
                    "USMAP extensions are not supported; supply a base mapping without extensions");
    for (const auto &pair : p->schemas) {
        const auto *schema = &pair.second;
        std::set<std::string> seen;
        while (schema) {
            options.control.check();
            require(seen.insert(schema->name).second, "Cyclic USMAP inheritance");
            if (seen.size() > options.max_depth)
                throw Error("schema-limit", "USMAP inheritance depth exceeds limit");
            if (schema->super.empty())
                break;
            auto it = p->schemas.find(schema->super);
            require(it != p->schemas.end(), "Missing USMAP base schema");
            schema = &it->second;
        }
    }
    p->hash = sha256_bytes(bytes, options.control);
    return UnversionedSchemas(p);
}
const SchemaDefinition &UnversionedSchemas::schema(const std::string &name) const {
    auto it = impl_->schemas.find(name);
    if (it == impl_->schemas.end())
        throw Error("schema-missing", "No USMAP schema for: " + name);
    return it->second;
}
const std::string &UnversionedSchemas::sha256() const {
    return impl_->hash;
}
const Json &UnversionedSchemas::versioning() const {
    return impl_->versioning;
}
Json UnversionedSchemas::to_json() const {
    Json schemas = Json::array();
    for (const auto &pair : impl_->schemas)
        schemas.push_back({{"name", pair.first},
                           {"super", pair.second.super},
                           {"slots", pair.second.slots},
                           {"mappedSlots", pair.second.properties.size()}});
    return {{"schema", "cyuediff.usmap/1"},    {"version", impl_->version}, {"sha256", impl_->hash},
            {"versioning", impl_->versioning}, {"schemas", schemas},        {"enums", impl_->enums}};
}
namespace {
struct PropertyReader {
    Reader r;
    const UnversionedSchemas &schemas;
    const UnversionedReadOptions &options;
    UnversionedProperties result;
    void charge() {
        options.control.check();
        if (result.values_read >= options.max_values)
            throw Error("schema-limit", "Property work exceeds configured limit", r.pos);
        if (options.charge)
            options.charge(1, 0);
        ++result.values_read;
    }
    void text(std::uint64_t n) {
        if (n > options.max_text_bytes - result.text_bytes)
            throw Error("schema-limit", "Decoded property text exceeds limit", r.pos);
        if (options.charge)
            options.charge(0, n);
        result.text_bytes += n;
    }
    void depth(unsigned n) {
        if (n >= options.max_depth)
            throw Error("schema-limit", "Property nesting exceeds limit", r.pos);
    }
    std::uint32_t slots(const SchemaDefinition &schema) {
        auto *s = &schema;
        std::uint32_t count = 0;
        for (unsigned d = 0;; ++d) {
            depth(d);
            count += s->slots;
            if (s->super.empty())
                return count;
            s = &schemas.schema(s->super);
        }
    }
    void field(const SchemaDefinition &root, std::uint32_t index, bool zero, unsigned nesting, Json &object) {
        auto *schema = &root;
        auto local = index;
        for (unsigned d = 0; local >= schema->slots; ++d) {
            depth(d);
            local -= schema->slots;
            if (schema->super.empty())
                throw Error("schema-mismatch", "Property index exceeds supplied schema", r.pos);
            schema = &schemas.schema(schema->super);
        }
        auto it = schema->properties.find(static_cast<std::uint16_t>(local));
        if (it == schema->properties.end())
            throw Error("schema-missing", "Serialized property slot has no type mapping", r.pos);
        const auto &p = it->second;
        auto key = schema->name + "." + p.name;
        if (p.array_size > 1)
            key += "[" + std::to_string(local - p.index) + "]";
        text(key.size());
        Json value = {{"type", type_name(p.type.kind)},
                      {"serializedZero", zero},
                      {"schemaIndex", index},
                      {"value", read_value(p.type, zero, nesting + 1)}};
        if (!p.type.name.empty()) {
            text(p.type.name.size());
            value["typeName"] = p.type.name;
        }
        object[key] = std::move(value);
    }
    Json structure(const std::string &name, bool zero, unsigned nesting) {
        depth(nesting);
        const auto &schema = schemas.schema(name);
        auto total_slots = slots(schema);
        Json object = Json::object();
        if (zero) {
            for (unsigned j = 0; j < total_slots; ++j)
                field(schema, j, true, nesting, object);
            return object;
        }
        struct Fragment {
            unsigned skip, count;
            bool mask, last;
        };
        std::vector<Fragment> fragments;
        std::uint64_t masked = 0, index = 0;
        for (;;) {
            charge();
            auto packed = static_cast<unsigned>(r.number(2));
            Fragment f{packed & 127, packed >> 9, (packed & 128) != 0, (packed & 256) != 0};
            index += f.skip + f.count;
            if (index > total_slots)
                throw Error("schema-mismatch", "Unversioned fragment exceeds schema slots", r.pos - 2);
            if (f.mask)
                masked += f.count;
            fragments.push_back(f);
            if (f.last)
                break;
        }
        auto mask_bytes =
            masked <= 8 ? (masked ? 1ull : 0ull) : (masked <= 16 ? 2ull : ((masked + 31) / 32) * 4);
        r.range(mask_bytes);
        auto mask_at = r.pos;
        r.skip(mask_bytes);
        index = 0;
        std::uint64_t bit = 0;
        for (const auto &f : fragments) {
            index += f.skip;
            for (unsigned j = 0; j < f.count; ++j) {
                bool is_zero = false;
                if (f.mask) {
                    is_zero = (r.data[static_cast<std::size_t>(mask_at + bit / 8)] & (1u << (bit % 8))) != 0;
                    ++bit;
                }
                field(schema, static_cast<std::uint32_t>(index++), is_zero, nesting, object);
            }
        }
        return object;
    }
    Json read_value(const SchemaType &type, bool zero, unsigned nesting) {
        depth(nesting);
        charge();
        auto integer = [&](unsigned bytes, bool sign) -> Json {
            auto n = zero ? 0ull : r.number(bytes);
            if (!sign)
                return n;
            if (bytes < 8 && (n & (1ull << (bytes * 8 - 1))))
                n |= (~0ull) << (bytes * 8);
            return static_cast<std::int64_t>(n);
        };
        switch (type.kind) {
        case 0:
            return integer(1, false);
        case 1: {
            auto v = zero ? 0 : r.number(1);
            if (v > 1)
                throw Error("schema-value", "Invalid boolean property", r.pos);
            return v != 0;
        }
        case 2:
            return integer(4, true);
        case 18:
            return integer(8, false);
        case 19:
            return integer(4, false);
        case 20:
            return integer(2, false);
        case 21:
            return integer(8, true);
        case 22:
            return integer(2, true);
        case 23:
            return integer(1, true);
        case 3:
        case 7: {
            double value = 0;
            if (!zero) {
                if (type.kind == 3) {
                    auto bits = static_cast<std::uint32_t>(r.number(4));
                    float f;
                    std::memcpy(&f, &bits, 4);
                    value = f;
                } else {
                    auto bits = r.number(8);
                    std::memcpy(&value, &bits, 8);
                }
            }
            if (!std::isfinite(value))
                throw Error("schema-value", "Non-finite property value", r.pos);
            return value;
        }
        case 4: {
            auto index = zero ? 0 : r.i32();
            if (!index)
                return nullptr;
            if (!options.reference)
                throw Error("schema-context", "Object property requires a package reference resolver", r.pos);
            auto value = options.reference(index);
            // Charge reference strings supplied by the host, including nested fields.
            std::function<void(const Json &)> measure = [&](const Json &v) {
                if (v.is_string())
                    text(v.get_ref<const std::string &>().size());
                else if (v.is_structured())
                    for (const auto &child : v)
                        measure(child);
            };
            measure(value);
            return value;
        }
        case 5: {
            if (zero) {
                text(4);
                return "None";
            }
            auto index = r.number(4), number = r.number(4);
            if (!options.names || index >= options.names->size())
                throw Error("schema-context", "Name property index exceeds package map", r.pos - 8);
            const auto &base = (*options.names)[static_cast<std::size_t>(index)];
            text(base.size() + 12);
            return number ? base + "_" + std::to_string(number - 1) : base;
        }
        case 10: {
            if (zero)
                return "";
            auto length = r.i32();
            if (!length)
                return "";
            bool wide = length < 0;
            auto n = wide ? -static_cast<std::int64_t>(length) : static_cast<std::int64_t>(length);
            if (std::uint64_t(n) > options.max_text_bytes - result.text_bytes)
                throw Error("schema-limit", "String property exceeds limit", r.pos - 4);
            r.range(std::uint64_t(n) * (wide ? 2 : 1));
            std::string value;
            for (std::int64_t j = 0; j < n; ++j) {
                auto cp = static_cast<std::uint32_t>(r.number(wide ? 2 : 1));
                if (j == n - 1) {
                    if (cp)
                        throw Error("schema-value", "Unterminated string property", r.pos);
                    break;
                }
                if (!cp)
                    throw Error("schema-value", "Embedded NUL in string property", r.pos);
                if (!wide && cp >= 128)
                    throw Error("schema-type",
                                "Non-ASCII narrow FString requires a host character-set reader", r.pos);
                if (wide && cp >= 0xd800 && cp <= 0xdbff) {
                    if (j + 1 >= n - 1)
                        throw Error("schema-value", "Truncated UTF-16 surrogate", r.pos);
                    auto low = r.number(2);
                    ++j;
                    if (low < 0xdc00 || low > 0xdfff)
                        throw Error("schema-value", "Invalid UTF-16 surrogate", r.pos);
                    cp = 0x10000 + ((cp - 0xd800) << 10) + static_cast<unsigned>(low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    throw Error("schema-value", "Unpaired UTF-16 surrogate", r.pos);
                auto old = value.size();
                append_utf8(value, cp);
                text(value.size() - old);
            }
            return value;
        }
        case 8: {
            auto count = zero ? 0 : r.count(options.max_values - result.values_read);
            Json values = Json::array();
            for (unsigned j = 0; j < count; ++j)
                values.push_back(read_value(type.arguments.at(0), false, nesting + 1));
            return values;
        }
        case 9:
            if (!options.reflected_structs.count(type.name))
                throw Error("schema-type", "Struct needs an explicit reflected layout: " + type.name, r.pos);
            return structure(type.name, zero, nesting + 1);
        case 26:
            if (type.arguments.empty() || !(type.arguments[0].kind == 0 || type.arguments[0].kind == 2 ||
                                            (type.arguments[0].kind >= 18 && type.arguments[0].kind <= 23)))
                throw Error("schema-type", "Enum requires an integer underlying type", r.pos);
            return read_value(type.arguments[0], zero, nesting + 1);
        default:
            throw Error("schema-type",
                        std::string("Unsupported unversioned value type: ") + type_name(type.kind), r.pos);
        }
    }
};
} // namespace
UnversionedProperties decode_unversioned(const std::vector<std::uint8_t> &bytes, std::uint64_t offset,
                                         std::uint64_t size, const UnversionedSchemas &schemas,
                                         const std::string &schema, const UnversionedReadOptions &options) {
    if (offset > bytes.size() || size > bytes.size() - offset)
        throw Error("bounds", "Invalid unversioned property window", offset);
    PropertyReader reader{{bytes, options.control, offset, offset + size}, schemas, options, {}};
    reader.result.properties = reader.structure(schema, false, 0);
    reader.result.consumed_bytes = reader.r.pos - offset;
    return std::move(reader.result);
}
} // namespace cyuediff
