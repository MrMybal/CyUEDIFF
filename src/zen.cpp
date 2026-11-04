#include <cyuediff/zen.hpp>
#include "archive.hpp"
#include <algorithm>
#include <array>
#include <set>
namespace cyuediff {
std::string ZenSchemaContext::fingerprint() const {
    Json bindings_json = Json::object();
    for (const auto &pair : bindings)
        bindings_json[pair.first] = {{"schema", pair.second.schema}, {"offset", pair.second.property_offset}};
    auto text = Json{{"profile", static_cast<unsigned>(profile)},
                     {"usmap", schemas ? schemas->sha256() : std::string{}},
                     {"bindings", bindings_json},
                     {"reflectedStructs", reflected_structs}}
                    .dump();
    return sha256_bytes(std::vector<std::uint8_t>(text.begin(), text.end()));
}
namespace {
constexpr std::uint64_t null_index = ~0ull, index_mask = (1ull << 62) - 1;
void require(bool condition, const char *message) {
    if (!condition)
        throw Error("zen-format", message);
}
std::string hex(std::uint64_t v, unsigned digits = 16) {
    static const char chars[] = "0123456789abcdef";
    std::string result(digits, '0');
    for (unsigned j = 0; j < digits; ++j) {
        result[digits - 1 - j] = chars[v & 15];
        v >>= 4;
    }
    return result;
}
struct Budget {
    const ZenReadOptions &options;
    std::uint64_t text = 0;
    void add(std::size_t bytes) {
        if (bytes > options.max_text_bytes - text)
            throw Error("zen-limit", "Cooked text exceeds configured limit");
        text += bytes;
    }
};
struct Reader {
    const std::vector<std::uint8_t> &data;
    const ZenReadOptions &options;
    std::uint64_t pos = 0, end;
    Reader(const std::vector<std::uint8_t> &bytes, const ZenReadOptions &o)
        : data(bytes), options(o), end(bytes.size()) {}
    void range(std::uint64_t size) const {
        options.control.check();
        if (pos > end || size > end - pos)
            throw Error("bounds", "Cooked read exceeds its source window", pos);
    }
    std::uint64_t number(unsigned bytes) {
        range(bytes);
        std::uint64_t n = 0;
        for (unsigned j = 0; j < bytes; ++j)
            n |= std::uint64_t(data[static_cast<std::size_t>(pos++)]) << (8 * j);
        return n;
    }
    std::uint32_t u32() {
        return static_cast<std::uint32_t>(number(4));
    }
    std::uint64_t u64() {
        return number(8);
    }
    void skip(std::uint64_t size) {
        range(size);
        pos += size;
    }
    void zeros(std::uint64_t size) {
        range(size);
        for (std::uint64_t j = 0; j < size; ++j)
            require(number(1) == 0, "Nonzero cooked alignment padding");
    }
    std::uint32_t count() {
        auto n = u32();
        if (n > options.max_entries)
            throw Error("zen-limit", "Cooked table exceeds configured limit");
        return n;
    }
};
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
std::vector<std::string> name_batch(Reader &r, Budget &budget) {
    auto count = r.count();
    if (!count)
        return {};
    auto bytes = r.u32();
    r.skip(8); // Stored name hash algorithm is not required to decode names.
    r.range(std::uint64_t(count) * 10 + bytes);
    r.skip(std::uint64_t(count) * 8);
    std::vector<std::uint16_t> headers;
    headers.reserve(count);
    for (unsigned j = 0; j < count; ++j) {
        auto hi = r.number(1), lo = r.number(1);
        headers.push_back(static_cast<std::uint16_t>((hi << 8) | lo));
    }
    auto start = r.pos, previous_end = r.end;
    r.end = start + bytes;
    std::vector<std::string> names;
    names.reserve(count);
    for (auto header : headers) {
        auto length = header & 0x7fff;
        bool wide = (header & 0x8000) != 0;
        if (std::uint64_t(length) * (wide ? 2 : 1) > r.options.max_name_bytes)
            throw Error("zen-limit", "Cooked name exceeds configured limit");
        require(length > 0, "Empty cooked name");
        std::string name;
        for (unsigned j = 0; j < static_cast<unsigned>(length); ++j) {
            auto cp = static_cast<std::uint32_t>(r.number(wide ? 2 : 1));
            require(cp != 0, "Embedded null in cooked name");
            if (wide && cp >= 0xd800 && cp <= 0xdbff) {
                require(j + 1 < static_cast<unsigned>(length), "Truncated cooked UTF-16 surrogate");
                auto low = r.number(2);
                ++j;
                require(low >= 0xdc00 && low <= 0xdfff, "Invalid cooked UTF-16 surrogate");
                cp = 65536 + ((cp - 0xd800) << 10) + static_cast<std::uint32_t>(low - 0xdc00);
            } else
                require(!wide || cp < 0xdc00 || cp > 0xdfff, "Invalid cooked UTF-16 surrogate");
            append_utf8(name, cp);
        }
        budget.add(name.size());
        names.push_back(std::move(name));
    }
    require(r.pos - start == bytes, "Name batch string byte count mismatch");
    r.end = previous_end;
    return names;
}
std::string mapped_name(std::uint32_t index, std::uint32_t number, const std::vector<std::string> &names,
                        unsigned type, Budget &budget) {
    require((index >> 30) == type && (index & 0x3fffffff) < names.size() && number != ~0u,
            "Invalid cooked mapped name");
    auto name = names[index & 0x3fffffff];
    if (number)
        name += "_" + std::to_string(number - 1);
    budget.add(name.size());
    return name;
}
std::string mapped_name(Reader &r, const std::vector<std::string> &names, unsigned type, Budget &budget) {
    auto index = r.u32(), number = r.u32();
    return mapped_name(index, number, names, type, budget);
}
void input_limit(const std::vector<std::uint8_t> &bytes, const ZenReadOptions &o) {
    o.control.check();
    if (bytes.size() > o.max_package_bytes)
        throw Error("zen-limit", "Cooked input exceeds configured limit");
}
void profile_check(ZenProfile profile) {
    if (profile != ZenProfile::unreal_5_3 && profile != ZenProfile::unreal_5_5)
        throw Error("zen-profile", "Select an explicit ue5.3 or ue5.5 cooked layout profile");
}
std::string join_path(const std::string &parent, const std::string &name, Budget &budget) {
    if (parent.size() + name.size() + 1 > budget.options.max_name_bytes)
        throw Error("zen-limit", "Cooked object path exceeds configured limit");
    auto path = parent.empty() ? name : parent + "." + name;
    budget.add(path.size());
    return path;
}
unsigned index_type(std::uint64_t index) {
    if (index == null_index)
        return 3;
    auto type = static_cast<unsigned>(index >> 62);
    require(type != 3, "Invalid cooked object reference tag");
    return type;
}
std::uint32_t table_count(std::uint64_t size, std::uint32_t stride, const ZenReadOptions &o) {
    require(size % stride == 0, "Misaligned cooked table size");
    auto n = size / stride;
    if (n > o.max_entries)
        throw Error("zen-limit", "Cooked table exceeds configured limit");
    return static_cast<std::uint32_t>(n);
}
} // namespace
ScriptObjects ScriptObjects::parse(const std::vector<std::uint8_t> &bytes, const ZenReadOptions &options) {
    input_limit(bytes, options);
    Reader r(bytes, options);
    Budget budget{options};
    auto names = name_batch(r, budget);
    auto count = r.count();
    r.range(std::uint64_t(count) * 32);
    struct Entry {
        std::string name;
        std::uint64_t outer;
    };
    std::map<std::uint64_t, Entry> entries;
    for (unsigned j = 0; j < count; ++j) {
        auto name = mapped_name(r, names, 2, budget);
        auto id = r.u64(), outer = r.u64(), cdo = r.u64();
        require(index_type(id) == 1 && (index_type(outer) == 1 || outer == null_index) &&
                    (index_type(cdo) == 1 || cdo == null_index),
                "Invalid global script object reference");
        require(entries.emplace(id, Entry{std::move(name), outer}).second,
                "Duplicate global script object ID");
    }
    require(r.pos == bytes.size(), "Trailing global script object bytes");
    ScriptObjects result;
    std::map<std::uint64_t, std::uint32_t> depths;
    for (const auto &[id, entry] : entries) {
        (void)entry;
        options.control.check();
        std::vector<std::uint64_t> chain;
        std::set<std::uint64_t> seen;
        auto current = id;
        while (current != null_index && !result.paths_.count(current)) {
            require(entries.count(current) != 0, "Missing global script object outer");
            require(seen.insert(current).second, "Cyclic global script object outer");
            if (chain.size() >= options.max_depth)
                throw Error("zen-limit", "Global script object depth exceeds limit");
            chain.push_back(current);
            current = entries.at(current).outer;
        }
        auto parent = current == null_index ? std::string{} : result.paths_.at(current);
        auto depth = current == null_index ? 0u : depths.at(current);
        if (depth + chain.size() > options.max_depth)
            throw Error("zen-limit", "Global script object depth exceeds limit");
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            parent = join_path(parent, entries.at(*it).name, budget);
            result.paths_[*it] = parent;
            depths[*it] = ++depth;
        }
    }
    result.sha256_ = sha256_bytes(bytes, options.control);
    options.control.checkpoint("cooked-script-objects", count, count);
    return result;
}
std::string ScriptObjects::resolve(std::uint64_t index) const {
    auto found = paths_.find(index);
    return found == paths_.end() ? std::string{} : found->second;
}

ZenPackage ZenPackage::parse(const std::vector<std::uint8_t> &bytes, const ZenReadOptions &options) {
    input_limit(bytes, options);
    profile_check(options.profile);
    Reader r(bytes, options);
    Budget budget{options};
    ZenPackage p;
    p.profile = options.profile == ZenProfile::unreal_5_3 ? "ue5.3" : "ue5.5";
    if (options.script_objects)
        p.script_objects_sha256 = options.script_objects->sha256();
    auto versioned = r.u32();
    require(versioned <= 1, "Invalid cooked versioning flag");
    p.header_size = r.u32();
    require(p.header_size >= 52 && p.header_size <= bytes.size(), "Invalid cooked header size");
    if (p.header_size > options.max_header_bytes)
        throw Error("zen-limit", "Cooked header exceeds configured limit");
    auto package_name_index = r.u32(), package_name_number = r.u32();
    p.flags = r.u32();
    p.cooked_header_size = r.u32();
    p.unversioned_properties = (p.flags & 0x2000) != 0;
    std::array<std::uint32_t, 8> offsets{};
    for (unsigned j = 0; j < 7; ++j)
        offsets[j] = r.u32();
    offsets[7] = p.header_size;
    require(offsets[0] >= 52, "Cooked table precedes the summary");
    for (unsigned j = 1; j < 8; ++j)
        require(offsets[j] >= offsets[j - 1] && offsets[j] <= p.header_size, "Invalid cooked table offsets");
    r.end = offsets[0];
    if (versioned) {
        auto zen = r.u32(), ue4 = r.u32(), ue5 = r.u32(), licensee = r.u32();
        if (zen != 3 || ue4 != 522 || licensee ||
            (options.profile == ZenProfile::unreal_5_3 ? ue5 != 1009 : (ue5 < 1012 || ue5 > 1013)))
            throw Error("zen-version", "Recorded cooked versions do not match the selected supported layout");
        p.versioning = {{"zen", zen},
                        {"ue4", ue4},
                        {"ue5", ue5},
                        {"licensee", licensee},
                        {"customVersions", Json::object()}};
        auto n = r.count();
        r.range(std::uint64_t(n) * 20);
        for (unsigned j = 0; j < n; ++j) {
            std::string guid;
            for (unsigned k = 0; k < 4; ++k)
                guid += hex(r.u32(), 8);
            require(!p.versioning["customVersions"].contains(guid), "Duplicate cooked custom version GUID");
            p.versioning["customVersions"][guid] = static_cast<std::int32_t>(r.u32());
        }
    }
    p.names = name_batch(r, budget);
    p.name = mapped_name(package_name_index, package_name_number, p.names, 0, budget);
    if (options.profile == ZenProfile::unreal_5_5) {
        auto padding = r.u64();
        require(padding <= 7, "Invalid cooked bulk alignment count");
        r.zeros(padding);
    }
    auto bulk_bytes = r.u64();
    auto bulk_count = table_count(bulk_bytes, 32, options);
    r.range(bulk_bytes);
    for (unsigned j = 0; j < bulk_count; ++j) {
        auto at = static_cast<std::int64_t>(r.u64()), duplicate = static_cast<std::int64_t>(r.u64()),
             size = static_cast<std::int64_t>(r.u64());
        auto flags = r.u32();
        auto index = r.number(1);
        r.skip(3);
        require(at >= -1 && duplicate >= -1 && size >= 0, "Invalid cooked bulk range");
        p.bulk_data.push_back({{"offset", at},
                               {"duplicateOffset", duplicate},
                               {"size", size},
                               {"flags", flags},
                               {"cookedIndex", index}});
    }
    if (options.profile == ZenProfile::unreal_5_5) {
        auto pad = (8 - r.pos % 8) % 8;
        r.zeros(pad);
    }
    require(r.pos == offsets[0], "Cooked name/bulk data does not end at the hash table");
    r.end = p.header_size;
    std::vector<std::uint64_t> public_hashes, raw_imports;
    for (unsigned j = 0, n = table_count(offsets[1] - offsets[0], 8, options); j < n; ++j)
        public_hashes.push_back(r.u64());
    for (unsigned j = 0, n = table_count(offsets[2] - offsets[1], 8, options); j < n; ++j)
        raw_imports.push_back(r.u64());
    auto ne = table_count(offsets[3] - offsets[2], 72, options);
    p.exports.reserve(ne);
    for (unsigned j = 0; j < ne; ++j) {
        ZenExport e;
        e.offset = r.u64();
        e.size = r.u64();
        e.name = mapped_name(r, p.names, 0, budget);
        e.outer_index = r.u64();
        e.class_index = r.u64();
        e.super_index = r.u64();
        e.template_index = r.u64();
        e.public_hash = r.u64();
        e.flags = r.u32();
        e.filter_flags = static_cast<std::uint8_t>(r.number(1));
        r.skip(3);
        require(e.filter_flags <= 2, "Invalid cooked export filter flags");
        if (e.offset > bytes.size() - p.header_size || e.size > bytes.size() - p.header_size - e.offset)
            throw Error("bounds", "Cooked export range exceeds its bundle", e.offset);
        e.offset += p.header_size;
        p.exports.push_back(std::move(e));
    }
    // Validate ownership before hashing: overlapping ranges must never multiply work.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    for (const auto &e : p.exports)
        if (e.size)
            ranges.emplace_back(e.offset, e.offset + e.size);
    std::sort(ranges.begin(), ranges.end());
    auto validated_end = std::uint64_t(p.header_size);
    for (const auto &range : ranges) {
        require(range.first >= validated_end, "Overlapping cooked export ranges");
        validated_end = range.second;
    }
    auto bundle_count = table_count(offsets[4] - offsets[3], 8, options);
    require(std::uint64_t(bundle_count) == std::uint64_t(ne) * 2, "Cooked export command count mismatch");
    std::vector<std::uint8_t> commands(ne, 0);
    for (unsigned j = 0; j < bundle_count; ++j) {
        auto index = r.u32(), command = r.u32();
        require(index < ne && command <= 1, "Invalid cooked export command");
        require(commands[index] == command, "Duplicate or out-of-order cooked export command");
        ++commands[index];
    }
    require(std::find(commands.begin(), commands.end(), 0) == commands.end() &&
                std::find(commands.begin(), commands.end(), 1) == commands.end(),
            "Missing cooked export command");
    struct Dependency {
        std::int32_t first;
        std::array<std::uint32_t, 4> counts;
    };
    std::vector<Dependency> dependency_headers;
    auto nd = table_count(offsets[5] - offsets[4], 20, options);
    require(nd == ne, "Cooked dependency header count mismatch");
    for (unsigned j = 0; j < nd; ++j) {
        Dependency d;
        d.first = static_cast<std::int32_t>(r.u32());
        for (auto &n : d.counts)
            n = r.u32();
        dependency_headers.push_back(d);
    }
    std::vector<std::int32_t> dependency_entries;
    for (unsigned j = 0, n = table_count(offsets[6] - offsets[5], 4, options); j < n; ++j)
        dependency_entries.push_back(static_cast<std::int32_t>(r.u32()));
    p.imported_package_names = name_batch(r, budget);
    for (auto &name : p.imported_package_names) {
        auto number = r.u32();
        require(number != ~0u, "Invalid imported package name number");
        if (number) {
            auto suffix = "_" + std::to_string(number - 1);
            budget.add(suffix.size());
            name += suffix;
        }
    }
    require(r.pos == p.header_size, "Trailing cooked header bytes");
    auto validate_reference = [&](std::uint64_t index) {
        auto type = index_type(index);
        if (type == 0)
            require((index & index_mask) < ne, "Invalid local cooked export reference");
        else if (type == 2) {
            auto hash = static_cast<std::uint32_t>(index);
            auto package = (index & index_mask) >> 32;
            require(hash < public_hashes.size(), "Invalid cooked public export hash reference");
            require(p.imported_package_names.empty() || package < p.imported_package_names.size(),
                    "Invalid cooked imported package reference");
        }
    };
    for (auto ref : raw_imports)
        validate_reference(ref);
    for (const auto &e : p.exports)
        for (auto ref : {e.outer_index, e.class_index, e.super_index, e.template_index})
            validate_reference(ref);
    auto external_name = [&](std::uint64_t index) {
        auto type = index_type(index);
        if (type == 3)
            return std::string{};
        if (type == 1) {
            auto path = options.script_objects ? options.script_objects->resolve(index) : std::string{};
            return path.empty() ? "$script:" + hex(index) : path;
        }
        auto package = (index & index_mask) >> 32;
        auto hash = static_cast<std::uint32_t>(index);
        auto name = p.imported_package_names.empty()
                        ? "$package-index:" + std::to_string(package)
                        : p.imported_package_names[static_cast<std::size_t>(package)];
        return name + "#" + hex(public_hashes[hash]);
    };
    std::set<std::string> paths;
    std::vector<std::uint32_t> depths(ne, 0);
    for (unsigned j = 0; j < ne; ++j) {
        options.control.check();
        std::vector<std::size_t> chain;
        std::set<std::size_t> seen;
        std::uint64_t current = j;
        while (index_type(current) == 0 && p.exports[static_cast<std::size_t>(current)].path.empty()) {
            require(seen.insert(static_cast<std::size_t>(current)).second, "Cyclic cooked export outer");
            if (chain.size() >= options.max_depth)
                throw Error("zen-limit", "Cooked object depth exceeds limit");
            chain.push_back(static_cast<std::size_t>(current));
            current = p.exports[static_cast<std::size_t>(current)].outer_index;
        }
        auto parent = index_type(current) == 0 ? p.exports[static_cast<std::size_t>(current)].path
                                               : (current == null_index ? p.name : external_name(current));
        auto depth = index_type(current) == 0 ? depths[static_cast<std::size_t>(current)] : 0u;
        if (depth + chain.size() > options.max_depth)
            throw Error("zen-limit", "Cooked object depth exceeds limit");
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            depths[*it] = ++depth;
            auto &e = p.exports[*it];
            e.path = join_path(parent, e.name, budget);
            require(paths.insert(e.path).second, "Duplicate cooked object path");
            parent = e.path;
        }
    }
    std::set<std::uint64_t> unresolved_scripts;
    auto reference = [&](std::uint64_t index) -> Json {
        auto type = index_type(index);
        if (type == 3)
            return nullptr;
        if (type == 0) {
            const auto &path = p.exports[static_cast<std::size_t>(index)].path;
            budget.add(path.size());
            return {{"kind", "export"}, {"path", path}};
        }
        if (type == 1) {
            auto path = options.script_objects ? options.script_objects->resolve(index) : std::string{};
            if (path.empty()) {
                unresolved_scripts.insert(index);
                return {{"kind", "script"}, {"id", hex(index)}, {"resolved", false}};
            }
            budget.add(path.size());
            return {{"kind", "script"}, {"path", path}, {"resolved", true}};
        }
        auto package = (index & index_mask) >> 32;
        auto hash = static_cast<std::uint32_t>(index);
        Json result = {{"kind", "package"}, {"publicExportHash", hex(public_hashes[hash])}};
        if (p.imported_package_names.empty())
            result["packageIndex"] = package;
        else {
            const auto &name = p.imported_package_names[static_cast<std::size_t>(package)];
            budget.add(name.size());
            result["package"] = name;
        }
        return result;
    };
    for (auto ref : raw_imports)
        p.imports.push_back(reference(ref));
    if (options.schema_context) {
        const auto &context = *options.schema_context;
        if (!context.schemas || context.profile != options.profile)
            throw Error("schema-context",
                        "Schema context must supply mappings for the selected cooked profile");
        const auto &recorded = context.schemas->versioning();
        if (!recorded.is_null()) {
            auto ue5 = recorded.at("ue5").get<int>();
            if (recorded.at("ue4") != 522 ||
                (options.profile == ZenProfile::unreal_5_3 ? ue5 != 1009 : (ue5 < 1012 || ue5 > 1013)))
                throw Error("schema-context", "USMAP recorded engine version disagrees with cooked profile");
        }
        p.schema_context_sha256 = context.fingerprint();
    }
    std::uint32_t property_work = 0;
    for (auto &e : p.exports) {
        e.references = {{"outer", reference(e.outer_index)},
                        {"class", reference(e.class_index)},
                        {"super", reference(e.super_index)},
                        {"template", reference(e.template_index)}};
        e.class_name = index_type(e.class_index) == 0
                           ? p.exports[static_cast<std::size_t>(e.class_index)].path
                           : external_name(e.class_index);
        e.sha256 = detail::hash_range(bytes, e.offset, e.size, options.control);
        if (options.schema_context) {
            const auto &context = *options.schema_context;
            auto binding = context.bindings.find(e.class_name);
            if (binding != context.bindings.end()) {
                const auto &b = binding->second;
                try {
                    if (!p.unversioned_properties)
                        throw Error("schema-context", "USMAP decoding requires unversioned properties");
                    if (b.property_offset > e.size)
                        throw Error("bounds", "Property offset exceeds cooked export", e.offset);
                    UnversionedReadOptions props;
                    props.names = &p.names;
                    props.reflected_structs = context.reflected_structs;
                    props.allow_map_replacement = options.profile == ZenProfile::unreal_5_5;
                    props.soft_path_layout = SoftPathLayout::ue5_top_level_names;
                    props.max_values = options.max_property_values;
                    props.charge = [&](std::uint32_t work, std::uint64_t text) {
                        if (work > options.max_property_values - property_work)
                            throw Error("schema-limit", "Cooked package property work exceeds limit");
                        property_work += work;
                        budget.add(static_cast<std::size_t>(text));
                    };
                    props.max_depth = options.max_depth;
                    props.max_text_bytes = options.max_text_bytes - budget.text;
                    props.control = options.control;
                    props.reference = [&](std::int32_t index) -> Json {
                        if (index > 0) {
                            auto at = static_cast<std::uint32_t>(index - 1);
                            if (at >= p.exports.size())
                                throw Error("schema-context", "Invalid property export index");
                            return {{"kind", "export"}, {"path", p.exports[at].path}};
                        }
                        if (index < 0) {
                            auto at = static_cast<std::uint64_t>(-static_cast<std::int64_t>(index) - 1);
                            if (at >= p.imports.size())
                                throw Error("schema-context", "Invalid property import index");
                            return p.imports[static_cast<std::size_t>(at)];
                        }
                        return nullptr;
                    };
                    auto decoded =
                        decode_unversioned(bytes, e.offset + b.property_offset, e.size - b.property_offset,
                                           *context.schemas, b.schema, props);
                    e.property_read = {{"status", "decoded"},
                                       {"schema", b.schema},
                                       {"offset", b.property_offset},
                                       {"consumedBytes", decoded.consumed_bytes},
                                       {"properties", std::move(decoded.properties)},
                                       {"defaultsResolved", false},
                                       {"mappingVerified", false},
                                       {"nativePayloadDecoded", false}};
                    ++p.property_exports_decoded;
                } catch (const Error &error) {
                    // Budgets and cancellation are operation-wide failures, never opaque fallbacks.
                    if (error.code == "cancelled" || error.code == "schema-limit" ||
                        error.code == "zen-limit")
                        throw;
                    e.property_read = {{"status", "opaque"},
                                       {"schema", b.schema},
                                       {"error", error.code},
                                       {"message", error.what()}};
                    ++p.property_exports_failed;
                }
            }
        }
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> dependency_ranges;
    for (const auto &d : dependency_headers) {
        std::uint64_t total = 0;
        for (auto n : d.counts)
            total += n;
        require(d.first >= -1 && (d.first != -1 || total == 0), "Invalid cooked dependency start");
        auto first = d.first == -1 ? 0 : static_cast<std::uint64_t>(d.first);
        require(first <= dependency_entries.size() && total <= dependency_entries.size() - first,
                "Cooked dependency range exceeds table");
        if (total)
            dependency_ranges.emplace_back(first, first + total);
    }
    std::sort(dependency_ranges.begin(), dependency_ranges.end());
    std::uint64_t dependency_end = 0;
    for (const auto &range : dependency_ranges) {
        require(range.first == dependency_end, "Overlapping or unclaimed cooked dependency entries");
        dependency_end = range.second;
    }
    require(dependency_end == dependency_entries.size(), "Unclaimed cooked dependency entries");
    for (unsigned j = 0; j < ne; ++j) {
        const auto &d = dependency_headers[j];
        std::uint64_t total = 0;
        for (auto n : d.counts)
            total += n;
        require(d.first >= -1 && (d.first != -1 || total == 0), "Invalid cooked dependency start");
        auto first = d.first == -1 ? 0 : static_cast<std::uint64_t>(d.first);
        require(first <= dependency_entries.size() && total <= dependency_entries.size() - first,
                "Cooked dependency range exceeds table");
        Json groups = Json::array();
        for (auto n : d.counts) {
            Json group = Json::array();
            for (unsigned k = 0; k < n; ++k) {
                auto dep = static_cast<std::int64_t>(dependency_entries[static_cast<std::size_t>(first++)]);
                if (dep > 0) {
                    require(static_cast<std::uint64_t>(dep) <= ne, "Invalid dependency export index");
                    group.push_back(reference(static_cast<std::uint64_t>(dep - 1)));
                } else if (dep < 0) {
                    auto index = static_cast<std::uint64_t>(-dep - 1);
                    require(index < raw_imports.size(), "Invalid dependency import index");
                    group.push_back(reference(raw_imports[static_cast<std::size_t>(index)]));
                } else
                    group.push_back(nullptr);
            }
            // Dependency ordering has no meaning within a command group.
            std::sort(group.begin(), group.end(),
                      [](const Json &a, const Json &b) { return a.dump() < b.dump(); });
            groups.push_back(std::move(group));
        }
        p.dependencies.push_back({{"object", p.exports[j].path}, {"commandGroups", std::move(groups)}});
    }
    auto at = std::uint64_t(p.header_size);
    auto gap = [&](std::uint64_t end) {
        if (end > at)
            p.other_payload.push_back({{"offset", at},
                                       {"size", end - at},
                                       {"sha256", detail::hash_range(bytes, at, end - at, options.control)}});
    };
    for (const auto &range : ranges) {
        require(range.first >= at, "Overlapping cooked export ranges");
        gap(range.first);
        at = range.second;
    }
    gap(bytes.size());
    p.sha256 = sha256_bytes(bytes, options.control);
    p.header_sha256 = detail::hash_range(bytes, 0, p.header_size, options.control);
    p.diagnostics.push_back(
        "Object tables and references are decoded. Property regions require explicit matching schema "
        "bindings; "
        "native payloads remain opaque. External bulk data is not included in package-chunk equality.");
    if (!unresolved_scripts.empty())
        p.diagnostics.push_back(
            std::to_string(unresolved_scripts.size()) +
            " script imports are unresolved; supply this build's global script object table.");
    if (!versioned)
        p.diagnostics.push_back("Header layout selected explicitly from the host profile; no serialized "
                                "versioning info is present.");
    options.control.checkpoint("cooked-package", ne, ne);
    return p;
}

Json ZenExport::to_json() const {
    return {{"name", name},
            {"path", path},
            {"class", class_name},
            {"offset", offset},
            {"size", size},
            {"flags", flags},
            {"filterFlags", filter_flags},
            {"publicExportHash", hex(public_hash)},
            {"references", references},
            {"rawReferences",
             {{"outer", hex(outer_index)},
              {"class", hex(class_index)},
              {"super", hex(super_index)},
              {"template", hex(template_index)}}},
            {"coverage", "opaque-body"},
            {"propertyRead", property_read},
            {"sha256", sha256}};
}
Json ZenPackage::snapshot() const {
    Json objects = Json::object(), deps = Json::object(), gaps = Json::array();
    for (const auto &e : exports) {
        objects[e.path] = {{"name", e.name},
                           {"class", e.class_name},
                           {"references", e.references},
                           {"flags", e.flags},
                           {"filterFlags", e.filter_flags},
                           {"publicExportHash", hex(e.public_hash)},
                           {"opaque", {{"size", e.size}, {"sha256", e.sha256}}}};
        if (!e.property_read.is_null()) {
            if (e.property_read.at("status") == "decoded")
                objects[e.path]["properties"] = e.property_read.at("properties");
            else
                objects[e.path]["propertyRead"] = e.property_read;
        }
    }
    for (const auto &d : dependencies)
        deps[d.at("object").get<std::string>()] = d.at("commandGroups");
    for (const auto &gap : other_payload)
        gaps.push_back({{"size", gap.at("size")}, {"sha256", gap.at("sha256")}});
    return {{"package", name},      {"flags", flags},      {"versioning", versioning},
            {"objects", objects},   {"imports", imports},  {"bulkData", bulk_data},
            {"dependencies", deps}, {"otherPayload", gaps}};
}
Json ZenPackage::to_json() const {
    Json es = Json::array();
    for (const auto &e : exports)
        es.push_back(e.to_json());
    return {{"schema", "cyuediff.zen-package/1"},
            {"coverage", property_exports_decoded ? "object-tables-properties-and-opaque-bodies"
                                                  : "object-tables-and-opaque-bodies"},
            {"name", name},
            {"profile", profile},
            {"scriptObjectsSha256", script_objects_sha256},
            {"sha256", sha256},
            {"headerSha256", header_sha256},
            {"headerSize", header_size},
            {"cookedHeaderSize", cooked_header_size},
            {"flags", flags},
            {"unversionedProperties", unversioned_properties},
            {"propertiesDecoded", property_exports_decoded != 0},
            {"propertyExportsDecoded", property_exports_decoded},
            {"propertyExportsFailed", property_exports_failed},
            {"schemaContextSha256", schema_context_sha256},
            {"externalBulkCompared", false},
            {"versioning", versioning},
            {"names", names},
            {"importedPackageNames", imported_package_names},
            {"imports", imports},
            {"exports", es},
            {"bulkData", bulk_data},
            {"dependencies", dependencies},
            {"otherPayload", other_payload},
            {"diagnostics", diagnostics}};
}
namespace {
OperationControl combined_control(const OperationControl &a, const OperationControl &b) {
    OperationControl c;
    c.cancelled = [a, b] { return (a.cancelled && a.cancelled()) || (b.cancelled && b.cancelled()); };
    c.progress = [a, b](const OperationProgress &p) {
        if (a.progress)
            a.progress(p);
        if (b.progress)
            b.progress(p);
    };
    return c;
}
void walk(const Json &a, const Json &b, const std::string &path, Json &changes,
          const OperationControl &control) {
    control.check();
    if (a == b)
        return;
    auto token = [](const std::string &s) {
        std::string escaped;
        for (char c : s)
            escaped += c == '~' ? "~0" : (c == '/' ? "~1" : std::string(1, c));
        return escaped;
    };
    if (a.is_object() && b.is_object()) {
        for (auto it = a.begin(); it != a.end(); ++it) {
            auto child = path + "/" + token(it.key());
            if (!b.contains(it.key()))
                changes.push_back({{"op", "remove"}, {"path", child}, {"before", it.value()}});
            else
                walk(it.value(), b.at(it.key()), child, changes, control);
        }
        for (auto it = b.begin(); it != b.end(); ++it)
            if (!a.contains(it.key()))
                changes.push_back(
                    {{"op", "add"}, {"path", path + "/" + token(it.key())}, {"after", it.value()}});
    } else if (a.is_array() && b.is_array() && a.size() == b.size()) {
        for (std::size_t j = 0; j < a.size(); ++j)
            walk(a[j], b[j], path + "/" + std::to_string(j), changes, control);
    } else
        changes.push_back({{"op", "replace"}, {"path", path}, {"before", a}, {"after", b}});
}
} // namespace
ZenPackage read_zen_package(const Container &container, std::size_t entry, const ZenReadOptions &options,
                            const ContainerReadOptions &read_options) {
    profile_check(options.profile);
    options.control.check();
    read_options.control.check();
    if (entry >= container.entries().size() || container.entries()[entry].chunk_type != 1)
        throw Error("zen-entry", "Expected an IoStore export-bundle chunk (type 1)");
    auto read = read_options;
    read.max_payload_bytes = std::min(read.max_payload_bytes, options.max_package_bytes);
    auto parse = options;
    parse.control = combined_control(options.control, read_options.control);
    read.control = parse.control;
    return ZenPackage::parse(container.read(entry, read), parse);
}
Json compare_zen_packages(const ZenPackage &before, const ZenPackage &after,
                          const OperationControl &control) {
    control.checkpoint("cooked-diff");
    Json changes = Json::array();
    walk(before.snapshot(), after.snapshot(), "", changes, control);
    bool binary = before.sha256 != after.sha256;
    auto status = changes.empty() ? (binary ? "binary-only" : "identical") : "changed";
    control.checkpoint("cooked-diff", 1, 1);
    return {
        {"schema", "cyuediff.zen-diff/1"},
        {"status", status},
        {"coverage", before.property_exports_decoded || after.property_exports_decoded
                         ? "object-tables-properties-and-opaque-bodies"
                         : "object-tables-and-opaque-bodies"},
        {"binaryChanged", binary},
        {"headerBinaryChanged", before.header_sha256 != after.header_sha256},
        {"propertiesDecoded", before.property_exports_decoded != 0 || after.property_exports_decoded != 0},
        {"beforePropertyExportsDecoded", before.property_exports_decoded},
        {"afterPropertyExportsDecoded", after.property_exports_decoded},
        {"beforeSchemaContextSha256", before.schema_context_sha256},
        {"afterSchemaContextSha256", after.schema_context_sha256},
        {"externalBulkCompared", false},
        {"beforeProfile", before.profile},
        {"afterProfile", after.profile},
        {"contextChanged", before.profile != after.profile ||
                               before.script_objects_sha256 != after.script_objects_sha256 ||
                               before.schema_context_sha256 != after.schema_context_sha256},
        {"beforeScriptObjectsSha256", before.script_objects_sha256},
        {"afterScriptObjectsSha256", after.script_objects_sha256},
        {"changes", changes}};
}
Json catalog_zen_packages(const Container &container, const ZenReadOptions &options,
                          const ContainerReadOptions &read_options,
                          const ZenCatalogOptions &catalog_options) {
    profile_check(options.profile);
    Json packages = Json::array(), errors = Json::array();
    std::uint64_t exports = 0, unversioned = 0, total = 0, done = 0, decoded = 0, failed = 0;
    for (const auto &e : container.entries())
        if (e.chunk_type == 1)
            ++total;
    auto control = combined_control(options.control, read_options.control);
    std::uint64_t report_bytes = 0;
    auto measure = [&](const Json &value) {
        std::uint64_t size = 0;
        auto add = [&](std::uint64_t bytes) {
            if (bytes > catalog_options.max_report_bytes - size)
                throw Error("zen-catalog-limit", "Cooked catalogue report exceeds configured limit");
            size += bytes;
        };
        std::function<void(const Json &)> visit = [&](const Json &v) {
            control.check();
            if (v.is_string())
                add(2 + 6 * std::uint64_t(v.get_ref<const std::string &>().size()));
            else if (v.is_array()) {
                add(2 + v.size());
                for (const auto &child : v)
                    visit(child);
            } else if (v.is_object()) {
                add(2 + v.size());
                for (auto it = v.begin(); it != v.end(); ++it) {
                    add(3 + 6 * std::uint64_t(it.key().size()));
                    visit(it.value());
                }
            } else
                add(32);
        };
        visit(value);
        return size;
    };
    for (std::size_t j = 0; j < container.entries().size(); ++j) {
        control.check();
        const auto &e = container.entries()[j];
        if (e.chunk_type != 1)
            continue;
        try {
            if (done >= catalog_options.max_packages)
                throw Error("zen-catalog-limit", "Cooked catalogue package limit reached");
            auto p = read_zen_package(container, j, options, read_options);
            if (p.exports.size() > catalog_options.max_exports - exports)
                throw Error("zen-catalog-limit", "Cooked catalogue export limit reached");
            Json classes = Json::object();
            for (const auto &object : p.exports)
                classes[object.class_name] = classes.value(object.class_name, std::uint64_t(0)) + 1;
            Json record = Json{{"id", e.id},
                               {"path", e.path},
                               {"name", p.name},
                               {"exports", p.exports.size()},
                               {"unversionedProperties", p.unversioned_properties},
                               {"classes", classes},
                               {"sha256", p.sha256}};
            if (catalog_options.include_snapshots)
                record["snapshot"] = p.snapshot();
            auto bytes = measure(record);
            if (bytes > catalog_options.max_report_bytes - report_bytes)
                throw Error("zen-catalog-limit", "Cooked catalogue report exceeds configured limit");
            report_bytes += bytes;
            packages.push_back(std::move(record));
            exports += p.exports.size();
            decoded += p.property_exports_decoded;
            failed += p.property_exports_failed;
            if (p.unversioned_properties)
                ++unversioned;
        } catch (const Error &error) {
            if (error.code == "cancelled")
                throw;
            if (error.code == "zen-catalog-limit") {
                errors.push_back({{"error", error.code}, {"message", error.what()}});
                break;
            }
            Json failure = {{"id", e.id},
                            {"path", e.path},
                            {"error", error.code},
                            {"message", error.what()},
                            {"offset", error.offset}};
            try {
                auto bytes = measure(failure);
                if (bytes > catalog_options.max_report_bytes - report_bytes)
                    throw Error("zen-catalog-limit", "Cooked catalogue report exceeds configured limit");
                report_bytes += bytes;
                errors.push_back(std::move(failure));
            } catch (const Error &limit) {
                if (limit.code == "cancelled")
                    throw;
                errors.push_back({{"error", limit.code}, {"message", limit.what()}});
                break;
            }
        }
        control.checkpoint("cooked-catalog", ++done, total);
    }
    return {
        {"schema", "cyuediff.zen-catalog/1"},
        {"profile", options.profile == ZenProfile::unreal_5_3 ? "ue5.3" : "ue5.5"},
        {"scriptObjectsSha256", options.script_objects ? options.script_objects->sha256() : std::string{}},
        {"scriptObjectCount", options.script_objects ? options.script_objects->size() : 0},
        {"complete", errors.empty()},
        {"coverage",
         decoded ? "object-tables-properties-and-opaque-bodies" : "object-tables-and-opaque-bodies"},
        {"packageCount", packages.size()},
        {"exportCount", exports},
        {"unversionedPackages", unversioned},
        {"propertiesDecoded", decoded != 0},
        {"schemaContextSha256",
         options.schema_context ? options.schema_context->fingerprint() : std::string{}},
        {"propertyExportsDecoded", decoded},
        {"propertyExportsFailed", failed},
        {"externalBulkCompared", false},
        {"packages", packages},
        {"errors", errors}};
}
} // namespace cyuediff
