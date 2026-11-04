#include <cyuediff/schema_context.hpp>
#include <algorithm>
#include <fstream>
namespace cyuediff {
namespace {
void fields(const Json &value, const std::set<std::string> &allowed) {
    if (!value.is_object())
        throw Error("schema-context", "Expected schema context object");
    for (auto it = value.begin(); it != value.end(); ++it)
        if (!allowed.count(it.key()))
            throw Error("schema-context", "Unknown schema context field: " + it.key());
}
std::string string_value(const Json &value) {
    if (!value.is_string())
        throw Error("schema-context", "Expected schema context string");
    auto text = value.get<std::string>();
    if (text.empty() || text.find('\0') != std::string::npos)
        throw Error("schema-context", "Schema context strings must be nonempty without NUL bytes");
    return text;
}
} // namespace
ZenSchemaManifest ZenSchemaManifest::read(const fs::path &path, const ZenSchemaManifestOptions &options) {
    options.control.checkpoint("schema-context");
    std::error_code ec;
    ZenSchemaManifest result;
    result.source_ = fs::absolute(path, ec).lexically_normal();
    if (ec || path.empty())
        throw Error("file-open", "Cannot resolve schema context path");
    std::ifstream file(result.source_, std::ios::binary | std::ios::ate);
    if (!file)
        throw Error("file-open", "Cannot open schema context: " + result.source_.u8string());
    auto size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > options.max_bytes ||
        static_cast<std::uint64_t>(size) > std::string{}.max_size())
        throw Error("schema-limit", "Schema context byte limit exceeded");
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    options.control.check();
    if (!file.read(text.data(), static_cast<std::streamsize>(text.size())) || file.peek() != EOF)
        throw Error("file-read", "Cannot read stable schema context bytes");
    options.control.check();
    try {
        std::vector<std::set<std::string>> keys;
        std::uint64_t text_bytes = 0;
        auto manifest = Json::parse(text, [&](int depth, Json::parse_event_t event, Json &value) {
            options.control.check();
            if (depth < 0 || static_cast<unsigned>(depth) > options.max_depth)
                throw Error("schema-limit", "Schema context nesting limit exceeded");
            if (event == Json::parse_event_t::object_start)
                keys.emplace_back();
            if (event == Json::parse_event_t::key ||
                (event == Json::parse_event_t::value && value.is_string())) {
                const auto &word = value.get_ref<const std::string &>();
                if (word.size() > options.max_string_bytes ||
                    word.size() > options.max_text_bytes - text_bytes)
                    throw Error("schema-limit", "Schema context text limit exceeded");
                text_bytes += word.size();
                if (event == Json::parse_event_t::key && !keys.back().insert(word).second)
                    throw Error("schema-context", "Duplicate schema context key");
            }
            if (event == Json::parse_event_t::object_end)
                keys.pop_back();
            return true;
        });
        result.format_ = string_value(manifest.at("format"));
        const bool native_format = result.format_ == "cyuediff.zen-schema/2";
        if (!native_format && result.format_ != "cyuediff.zen-schema/1")
            throw Error("schema-context", "Unsupported schema context format");
        auto allowed =
            std::set<std::string>{"format", "profile", "usmap", "sha256", "bindings", "reflectedStructs"};
        if (native_format)
            allowed.insert("nativeStructs");
        fields(manifest, allowed);
        const auto profile = string_value(manifest.at("profile"));
        if (profile == "ue5.3")
            result.profile_ = ZenProfile::unreal_5_3;
        else if (profile == "ue5.5")
            result.profile_ = ZenProfile::unreal_5_5;
        else
            throw Error("schema-context", "Unsupported schema context profile");
        result.mapping_ = fs::u8path(string_value(manifest.at("usmap")));
        if (result.mapping_.is_relative())
            result.mapping_ = result.source_.parent_path() / result.mapping_;
        result.mapping_ = result.mapping_.lexically_normal();
        result.expected_sha256_ = string_value(manifest.at("sha256"));
        if (result.expected_sha256_.size() != 64 ||
            !std::all_of(result.expected_sha256_.begin(), result.expected_sha256_.end(),
                         [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            throw Error("schema-context", "USMAP fingerprint must be 64 lowercase hexadecimal digits");
        const auto &bindings = manifest.at("bindings");
        if (!bindings.is_object() || bindings.size() > options.max_entries)
            throw Error("schema-limit", "Schema binding count limit exceeded");
        for (auto it = bindings.begin(); it != bindings.end(); ++it) {
            options.control.check();
            fields(it.value(), {"schema", "offset"});
            if (it.key().empty() || it.key()[0] != '/' || it.key().find('.') == std::string::npos ||
                it.key().find('\0') != std::string::npos)
                throw Error("schema-context", "Bindings require exact qualified class paths");
            const auto name = string_value(it.value().at("schema"));
            const auto &offset = it.value().at("offset");
            if (!offset.is_number_unsigned())
                throw Error("schema-context", "Property offset must be an unsigned integer");
            result.bindings_.emplace(it.key(), ZenSchemaBinding{name, offset.get<std::uint64_t>()});
        }
        if (manifest.contains("reflectedStructs")) {
            const auto &values = manifest.at("reflectedStructs");
            if (!values.is_array())
                throw Error("schema-context", "Expected reflectedStructs array");
            if (values.size() > options.max_entries - bindings.size())
                throw Error("schema-limit", "Schema context entry limit exceeded");
            std::set<std::string> unique;
            for (const auto &value : values) {
                options.control.check();
                auto name = string_value(value);
                if (!unique.insert(name).second)
                    throw Error("schema-context", "Duplicate reflected struct binding");
                result.reflected_structs_.push_back(std::move(name));
            }
        }
        if (native_format) {
            const auto &values = manifest.at("nativeStructs");
            if (!values.is_object())
                throw Error("schema-context", "Expected nativeStructs object");
            if (values.size() > options.max_entries - bindings.size() - result.reflected_structs_.size())
                throw Error("schema-limit", "Schema context entry limit exceeded");
            for (auto it = values.begin(); it != values.end(); ++it) {
                options.control.check();
                auto name = string_value(it.key());
                if (std::find(result.reflected_structs_.begin(), result.reflected_structs_.end(), name) !=
                    result.reflected_structs_.end())
                    throw Error("schema-context", "Conflicting native/reflected struct binding");
                result.native_structs_.emplace(std::move(name),
                                               parse_native_struct_layout(string_value(it.value())));
            }
        }
    } catch (const Json::exception &) {
        throw Error("schema-context", "Malformed schema context JSON or missing required field");
    } catch (const fs::filesystem_error &) {
        throw Error("schema-context", "Invalid USMAP path in schema context");
    }
    result.sha256_ = sha256_bytes(std::vector<std::uint8_t>(text.begin(), text.end()), options.control);
    options.control.checkpoint("schema-context", text.size(), text.size());
    return result;
}
std::shared_ptr<const ZenSchemaContext> ZenSchemaManifest::load(const SchemaReadOptions &options) const {
    options.control.checkpoint("schema-context");
    auto result = std::make_shared<ZenSchemaContext>();
    result->profile = profile_;
    result->schemas = std::make_shared<UnversionedSchemas>(UnversionedSchemas::read(mapping_, options));
    if (result->schemas->sha256() != expected_sha256_)
        throw Error("schema-context", "USMAP fingerprint disagrees with schema context");
    for (const auto &pair : bindings_) {
        options.control.check();
        result->schemas->schema(pair.second.schema);
        result->bindings.emplace(pair);
    }
    for (const auto &name : reflected_structs_) {
        options.control.check();
        const auto &definition = result->schemas->schema(name);
        if (!result->reflected_structs.insert(definition.path).second)
            throw Error("schema-context", "Duplicate reflected struct binding");
    }
    for (const auto &binding : native_structs_) {
        options.control.check();
        const auto &definition = result->schemas->schema(binding.first);
        if (result->reflected_structs.count(definition.path) ||
            !result->native_structs.emplace(definition.path, binding.second).second)
            throw Error("schema-context", "Conflicting native/reflected struct binding: " + definition.path);
    }
    options.control.check();
    return result;
}
Json ZenSchemaManifest::to_json() const {
    Json native = Json::object();
    for (const auto &binding : native_structs_)
        native[binding.first] = native_struct_layout_name(binding.second);
    return {{"schema", "cyuediff.zen-schema-manifest/1"},
            {"format", format_},
            {"nativeStructCount", native_structs_.size()},
            {"nativeStructs", native},
            {"coverage", "manifest-only"},
            {"source", source_.u8string()},
            {"manifestSha256", sha256_},
            {"profile", profile_ == ZenProfile::unreal_5_3 ? "ue5.3" : "ue5.5"},
            {"usmap", mapping_.u8string()},
            {"expectedUsmapSha256", expected_sha256_},
            {"bindingCount", bindings_.size()},
            {"reflectedStructCount", reflected_structs_.size()},
            {"mappingVerified", false}};
}
} // namespace cyuediff
