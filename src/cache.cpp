#include <cyuediff/cache.hpp>
#include <list>
#include <mutex>
#include <tuple>
namespace cyuediff {
namespace {
auto profile(const ReadOptions &o) {
    return std::make_tuple(o.properties, o.fingerprints, o.native_graphs, o.native_textures, o.native_meshes,
                           o.native_materials, o.native_skeletons, o.native_skeleton_retargeting,
                           o.native_skeleton_metadata, o.native_reflection, o.max_table_entries,
                           o.max_string_bytes, o.max_property_bytes, o.max_properties, o.max_depth,
                           o.decoders.get(), o.narrow_string_encoding, o.value_identities.names,
                           o.value_identities.max_names, o.value_identities.max_text_bytes);
}
struct Identity {
    Json files = Json::array();
    std::uint64_t bytes = 0;
};
Identity identity(const fs::path &path, const OperationControl &control, bool hash) {
    Identity result;
    for (const auto *extension : {"", ".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
        control.check();
        auto file = path;
        if (*extension)
            file.replace_extension(extension);
        std::error_code error;
        if (*extension && !fs::exists(file, error) && !error)
            continue;
        auto size = fs::file_size(file, error);
        if (error || size > UINT64_MAX - result.bytes)
            throw Error("io", "Cannot fingerprint package source: " + file.u8string());
        result.bytes += size;
        result.files.push_back({{"path", file.generic_u8string()},
                                {"bytes", size},
                                {"sha256", hash ? sha256_file(file, control) : std::string{}}});
    }
    return result;
}
} // namespace
struct PackageCache::State {
    struct Entry {
        fs::path path;
        ReadOptions options;
        Identity identity;
        std::shared_ptr<const Package> package;
    };
    PackageCacheOptions options;
    PackageCacheStats stats;
    std::list<Entry> entries;
    mutable std::mutex mutex;
    explicit State(const PackageCacheOptions &value) : options(value) {}
};
PackageCache::PackageCache(const PackageCacheOptions &options) : state_(std::make_unique<State>(options)) {
    if (!options.max_entries || options.max_entries > 1024 || !options.max_source_bytes)
        throw Error("cache-limit", "Invalid package cache limits");
}
PackageCache::~PackageCache() = default;
std::shared_ptr<const Package> PackageCache::open(const fs::path &file, const ReadOptions &options) {
    options.control.checkpoint("Checking package cache");
    auto path = fs::weakly_canonical(fs::absolute(file));
    if (options.value_identities.names && options.value_identities.charge) {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            ++state_->stats.misses;
        }
        return std::make_shared<const Package>(Package::open(path, options));
    }
    // Large inputs bypass retention without adding a redundant full hash pass.
    auto measured = identity(path, options.control, false);
    if (measured.bytes > state_->options.max_source_bytes) {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            ++state_->stats.misses;
        }
        return std::make_shared<const Package>(Package::open(path, options));
    }
    auto before = identity(path, options.control, true);
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        for (auto it = state_->entries.begin(); it != state_->entries.end(); ++it)
            if (it->path == path && profile(it->options) == profile(options) &&
                it->identity.files == before.files) {
                ++state_->stats.hits;
                auto result = it->package;
                state_->entries.splice(state_->entries.begin(), state_->entries, it);
                return result;
            }
        ++state_->stats.misses;
    }
    auto package = std::make_shared<const Package>(Package::open(path, options));
    auto after = identity(path, options.control, true);
    if (before.files != after.files)
        throw Error("source-changed", "Package source changed during cached read");
    if (after.bytes > state_->options.max_source_bytes)
        return package;
    ReadOptions saved = options;
    saved.control = {}; // Cached profiles never retain worker/session callbacks.
    saved.value_identities.charge = {};
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (auto it = state_->entries.begin(); it != state_->entries.end();) {
        if (it->path == path && profile(it->options) == profile(options)) {
            state_->stats.source_bytes -= it->identity.bytes;
            it = state_->entries.erase(it);
        } else
            ++it;
    }
    while (!state_->entries.empty() &&
           (state_->entries.size() >= state_->options.max_entries ||
            state_->stats.source_bytes > state_->options.max_source_bytes - after.bytes)) {
        state_->stats.source_bytes -= state_->entries.back().identity.bytes;
        state_->entries.pop_back();
    }
    state_->stats.source_bytes += after.bytes;
    state_->entries.push_front({path, std::move(saved), std::move(after), package});
    return package;
}
void PackageCache::clear() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->entries.clear();
    state_->stats.source_bytes = 0;
}
PackageCacheStats PackageCache::stats() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto result = state_->stats;
    result.entries = state_->entries.size();
    return result;
}
} // namespace cyuediff
