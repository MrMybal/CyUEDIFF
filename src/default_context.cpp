#include <cyuediff/defaults.hpp>
#include "blueprint_default_binding.hpp"
#include "sha256.hpp"
#include "string_value.hpp"
#include <fstream>
#include <set>
namespace cyuediff {
namespace {
void require(bool ok, const char *reason) {
    if (!ok)
        throw Error("default-context", reason);
}
bool digest(const std::string &value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string word(const Json &value) {
    const auto text = value.get<std::string>();
    require(!text.empty() && text.find('\0') == std::string::npos, "Invalid default context identity");
    return text;
}
std::uint64_t integer(const Json &value, std::uint64_t maximum) {
    require(value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0) &&
                value.get<std::uint64_t>() <= maximum,
            "Invalid default context integer");
    return value.get<std::uint64_t>();
}
fs::path file_path(const fs::path &base, const Json &value) {
    auto path = fs::u8path(word(value));
    if (path.is_relative())
        path = base / path;
    std::error_code ec;
    auto resolved = fs::canonical(path, ec);
    require(!ec && fs::is_regular_file(resolved, ec) && !ec,
            "Default context input is not a readable regular file");
    return resolved;
}
std::string file_digest(const fs::path &path, std::uint64_t expected, std::uint64_t maximum,
                        const OperationControl &control) {
    require(expected <= maximum, "Default context dependency exceeds byte budget");
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    require(bool(in) && in.tellg() >= 0 && static_cast<std::uint64_t>(in.tellg()) == expected,
            "Default context dependency size differs");
    in.seekg(0);
    detail::Sha256 sha;
    std::array<unsigned char, 64 * 1024> buffer{};
    std::uint64_t read = 0;
    while (read < expected) {
        control.check();
        auto count = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), expected - read));
        require(bool(in.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(count))),
                "Default context dependency read failed");
        sha.update(buffer.data(), count);
        read += count;
    }
    require(in.peek() == EOF, "Default context dependency changed size during verification");
    control.check();
    return sha.finish();
}
std::pair<Json, std::string> document(const fs::path &path, const SchemaReadOptions &o) {
    o.control.check();
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    require(bool(in), "Cannot open default context");
    const auto length = in.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > o.max_bytes ||
        static_cast<std::uint64_t>(length) > std::vector<std::uint8_t>{}.max_size())
        throw Error("default-limit", "Default context byte budget exceeded");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    in.seekg(0);
    for (std::size_t pos = 0; pos < bytes.size();) {
        o.control.check();
        const auto n = std::min<std::size_t>(64 * 1024, bytes.size() - pos);
        require(bool(in.read(reinterpret_cast<char *>(bytes.data() + pos), static_cast<std::streamsize>(n))),
                "Cannot read default context");
        pos += n;
    }
    require(in.peek() == EOF, "Default context changed size during read");
    std::uint64_t entries = 0, text = 0;
    std::vector<std::set<std::string>> keys;
    auto json = Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &value) {
        o.control.check();
        if (depth < 0 || static_cast<unsigned>(depth) >= o.max_depth)
            throw Error("default-limit", "Default context depth budget exceeded");
        if (event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start ||
            event == Json::parse_event_t::key || event == Json::parse_event_t::value) {
            if (entries >= o.max_entries)
                throw Error("default-limit", "Default context node budget exceeded");
            ++entries;
        }
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        if (event == Json::parse_event_t::key || (event == Json::parse_event_t::value && value.is_string())) {
            const auto &s = value.get_ref<const std::string &>();
            if (s.size() > o.max_text_bytes - text)
                throw Error("default-limit", "Default context text budget exceeded");
            text += s.size();
            detail::validate_byte_string(s, false, o.control, "default-context", 0);
            if (event == Json::parse_event_t::key)
                require(!keys.empty() && keys.back().insert(s).second, "Duplicate default context key");
        }
        if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
    return {std::move(json), sha256_bytes(bytes, o.control)};
}
} // namespace
struct DefaultContext::Impl {
    DefaultSnapshot snapshot;
    Json source, provenance;
    std::vector<fs::path> inputs;
    std::string hash;
    explicit Impl(DefaultSnapshot value) : snapshot(std::move(value)) {}
};
DefaultContext::DefaultContext(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
DefaultContext DefaultContext::read(const fs::path &path, const DefaultContextReadOptions &options) {
    try {
        auto parsed = document(path, options.document);
        const auto &doc = parsed.first;
        require(options.expected_sha256.empty() ||
                    (digest(options.expected_sha256) && options.expected_sha256 == parsed.second),
                "Default context fingerprint differs");
        const bool blueprint = doc.at("format") == "cyuediff.default-context/2";
        require(blueprint || doc.at("format") == "cyuediff.default-context/1",
                "Unsupported default context format");
        const auto mode = doc.value("inputMode", std::string("local-inputs"));
        require(mode == "local-inputs" || mode == "pinned-inputs", "Unsupported prepared input mode");
        const bool pinned = mode == "pinned-inputs";
        const auto &source = doc.at("sourceProfile");
        require(source.at("profile") == (blueprint ? "ue5.3.2-win64-blueprint-tagged-ascii/1"
                                                   : "ue5.3.2-win64-tagged-ascii/1") &&
                    integer(source.at("ue4"), INT32_MAX) == 522 &&
                    integer(source.at("ue5"), INT32_MAX) == 1009 &&
                    integer(source.at("licensee"), INT32_MAX) == 0 &&
                    source.at("customVersions").is_object() && source.at("intraPropertyDelta") == true,
                "Unsupported source default resolution profile");
        word(source.at("savedEngine"));
        for (auto it = source.at("customVersions").begin(); it != source.at("customVersions").end(); ++it) {
            require(it.key().size() == 32 &&
                        it.key().find_first_not_of("0123456789abcdef") == std::string::npos &&
                        it->is_number_integer() &&
                        (it->is_number_unsigned() ? it->get<std::uint64_t>() <= INT32_MAX
                                                  : (it->get<std::int64_t>() >= INT32_MIN &&
                                                     it->get<std::int64_t>() <= INT32_MAX)),
                    "Invalid source custom version");
        }
        const auto base = fs::absolute(path).parent_path();
        const auto &capture = doc.at("snapshot");
        auto read = options.snapshot;
        const auto engine = word(capture.at("engineVersion"));
        require(engine == "5.3.2-29314046+++UE5+Release-5.3" &&
                    (read.expected_engine_version.empty() || read.expected_engine_version == engine),
                "Unsupported prepared Engine version");
        require((read.expected_ue4 < 0 || read.expected_ue4 == 522) &&
                    (read.expected_ue5 < 0 || read.expected_ue5 == 1009),
                "Expected snapshot archive version differs");
        read.expected_engine_version = engine;
        read.expected_ue4 = 522;
        read.expected_ue5 = 1009;
        require(read.expected_sha256.empty() || read.expected_sha256 == capture.at("sha256"),
                "Expected snapshot fingerprint differs");
        read.expected_sha256 = word(capture.at("sha256"));
        require(digest(read.expected_sha256), "Invalid default snapshot fingerprint");
        require(capture.at("filterEditorOnly").is_boolean(), "Invalid prepared editor filter");
        read.filter_editor_only = capture.at("filterEditorOnly").get<bool>();
        read.values.value_identities.names = true;
        read.values.value_identities.max_names = read.values.max_values;
        read.values.value_identities.max_text_bytes = read.values.max_text_bytes;
        if (capture.contains("nativeStructs")) {
            require(capture.at("nativeStructs").is_object(), "Expected explicit captured native layouts");
            for (auto it = capture.at("nativeStructs").begin(); it != capture.at("nativeStructs").end(); ++it)
                read.values.native_structs[it.key()] = parse_native_struct_layout(word(it.value()));
        }
        if (capture.contains("textLayout")) {
            require(capture.at("textLayout") == "ue5-history", "Unsupported captured text layout");
            read.values.text_layout = TextLayout::ue5_history;
        }
        const auto previous = read.capture.control;
        read.capture.control.cancelled = [&] {
            return (previous.cancelled && previous.cancelled()) ||
                   (options.document.control.cancelled && options.document.control.cancelled());
        };
        auto p = std::make_shared<Impl>(DefaultSnapshot::read(file_path(base, capture.at("file")), read));
        p->inputs = {fs::absolute(path).lexically_normal(), file_path(base, capture.at("file"))};
        const auto &provenance = p->snapshot.provenance();
        if (!blueprint)
            require(provenance.at("blueprintClass") == false && provenance.at("classGeneratedBy").is_null(),
                    "Blueprint defaults require an explicit Blueprint profile");
        const auto cls = word(provenance.at("object").at("class"));
        if (!blueprint)
            require(cls.compare(0, 8, "/Script/") == 0, "Expected a native root class");
        for (const auto &structure : p->snapshot.structures())
            require(structure.contains("postSerialize") && structure.at("postSerialize").is_boolean(),
                    "Captured post-serializer policy unavailable");
        const auto &environment = provenance.at("environment");
        require(environment.at("format") == "cyuediff.engine-default-environment/1" &&
                    environment.at("platform") == "Windows" && environment.at("byteOrder") == "little" &&
                    environment.at("configurationFileIOFrozen") == true,
                "Unsupported prepared environment");
        const auto &files = environment.at("files");
        const auto &dependencies = doc.at("dependencies");
        require(files.is_object() && dependencies.is_object() && files.size() == dependencies.size() &&
                    files.size() <= options.max_dependencies && files.contains("executable") &&
                    files.contains("project") && files.contains("engine-build"),
                "Incomplete prepared dependency inventory");
        Json blueprint_binding = nullptr;
        if (blueprint)
            blueprint_binding = detail::validate_blueprint_default_binding(p->snapshot, source, environment,
                                                                           options.document.control);
        else {
            const auto dot = cls.find('.', 8);
            require(dot != std::string::npos && files.contains("module:" + cls.substr(8, dot - 8)),
                    "Native class module not recorded");
            require(provenance.at("object").at("path") ==
                        cls.substr(0, dot) + ".Default__" + cls.substr(dot + 1),
                    "Unsupported native class-default identity");
        }
        std::uint64_t total = 0;
        Json verified = Json::object();
        for (auto it = files.begin(); it != files.end(); ++it) {
            options.document.control.check();
            const auto &dep = dependencies.at(it.key());
            const auto role = word(it->at("role"));
            require(role == "module" || role == "config" || role == "project" || role == "engine-build" ||
                        role == "executable" ||
                        (blueprint && (role == "asset-package" || role == "asset-companion")),
                    "Unsupported dependency role");
            require(it->at("present").is_boolean(), "Expected captured dependency presence");
            const auto path_value = [&](const Json &value) {
                auto candidate = fs::u8path(word(value));
                if (candidate.is_relative())
                    candidate = base / candidate;
                // Optional hierarchy layers need no existing parent; normalize their spelling first.
                // Presence and exact file bytes are checked separately below.
                return fs::absolute(candidate).lexically_normal();
            };
            const auto file = path_value(dep.at("file"));
            p->inputs.push_back(file);
            if (!pinned)
                require(file == path_value(it->at("file")),
                        "Prepared dependency path differs from captured environment");
            if (it->at("present") == false) {
                require((role == "config" || (blueprint && role == "asset-companion")) &&
                            dep.at("absent") == true,
                        "Invalid captured configuration absence");
                if (!pinned) {
                    std::error_code ec;
                    const bool exists = fs::exists(file, ec);
                    require(!ec && !exists, "Prepared configuration absence changed");
                }
                verified[it.key()] = {{"role", role}, {"absent", true}};
                continue;
            }
            const auto size = integer(dep.at("bytes"), options.max_dependency_bytes - total);
            total += size;
            const auto sha = word(dep.at("sha256"));
            require(digest(sha), "Invalid dependency fingerprint");
            const auto captured_size = word(it->at("bytes"));
            require(captured_size == std::to_string(size) && it->at("sha256") == sha,
                    "Dependency fingerprint differs from the capture-time bytes");
            require(file_digest(file, size, options.max_dependency_bytes, options.document.control) == sha,
                    "Prepared dependency fingerprint differs");
            verified[it.key()] = {{"role", role}, {"sha256", sha}, {"bytes", size}};
            options.document.control.checkpoint("default-dependencies", verified.size(), files.size());
        }
        const auto &config = environment.at("mergedConfigBlake3");
        require(config.is_object(), "Expected captured merged configuration digests");
        for (auto it = config.begin(); it != config.end(); ++it)
            require(digest(word(it.value())) && files.contains(it.key()),
                    "Invalid captured configuration digest");
        p->source = source;
        p->hash = parsed.second;
        p->provenance = {{"binding", pinned ? "verified-pinned-inputs" : "verified-files-at-context-read"},
                         {"inputMode", mode},
                         {"contextSha256", p->hash},
                         {"captureSha256", p->snapshot.sha256()},
                         {"class", cls},
                         {"sourceProfile", source},
                         {"dependencies", std::move(verified)},
                         {"mergedConfigBlake3", config},
                         {"verifiedBytes", total}};
        if (blueprint)
            p->provenance["blueprintBinding"] = std::move(blueprint_binding);
        return DefaultContext(std::move(p));
    } catch (const Json::exception &) {
        throw Error("default-context", "Invalid default context document");
    }
}
const DefaultSnapshot &DefaultContext::snapshot() const { return impl_->snapshot; }
const std::vector<fs::path> &DefaultContext::input_files() const { return impl_->inputs; }
const Json &DefaultContext::source_profile() const { return impl_->source; }
const Json &DefaultContext::provenance() const { return impl_->provenance; }
const std::string &DefaultContext::sha256() const { return impl_->hash; }
} // namespace cyuediff
