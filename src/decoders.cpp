#include "decoders.hpp"
#include <cmath>
#include <set>

namespace cyuediff {
bool VersionRange::contains(int value) const {
    return value >= min && value <= max;
}
bool SerializationProfile::matches(const PackageSummary &summary) const {
    if (!ue4.contains(summary.ue4_version) || !ue5.contains(summary.ue5_version))
        return false;
    for (const auto &requirement : custom_versions) {
        auto it = summary.custom_versions.find(requirement.first);
        if (it == summary.custom_versions.end() || !it->is_number_integer() ||
            !requirement.second.contains(it->get<int>()))
            return false;
    }
    return true;
}
DecoderRegistry::DecoderRegistry(std::vector<AssetDecoder> entries) : entries_(std::move(entries)) {
    std::set<std::string> ids;
    auto range = [](VersionRange r) {
        if (r.min > r.max)
            throw Error("decoder-registration", "Invalid decoder version interval");
    };
    for (const auto &entry : entries_) {
        if (entry.id.empty() || entry.id.size() > 256 || entry.target.empty() || entry.target.size() > 4096 ||
            !entry.decode || !ids.insert(entry.id).second ||
            (entry.kind != DecoderKind::struct_value && entry.kind != DecoderKind::native_export))
            throw Error("decoder-registration", "Invalid or duplicate decoder registration");
        for (unsigned char c : entry.id)
            if (c < 0x21 || c > 0x7e)
                throw Error("decoder-registration", "Decoder ID must be printable ASCII without spaces");
        range(entry.profile.ue4);
        range(entry.profile.ue5);
        for (const auto &version : entry.profile.custom_versions) {
            if (version.first.size() != 32 ||
                version.first.find_first_not_of("0123456789abcdef") != std::string::npos)
                throw Error("decoder-registration", "Expected a lowercase 32-digit custom version GUID");
            range(version.second);
        }
        index_[{entry.kind, entry.target}].push_back(static_cast<std::size_t>(&entry - entries_.data()));
    }
}
const AssetDecoder *DecoderRegistry::find(DecoderKind kind, const std::string &target,
                                          const PackageSummary &summary) const {
    const AssetDecoder *match = nullptr;
    auto candidates = index_.find({kind, target});
    if (candidates == index_.end())
        return nullptr;
    for (auto index : candidates->second) {
        const auto &entry = entries_[index];
        if (entry.profile.matches(summary)) {
            if (match)
                throw Error("decoder-ambiguous", "Multiple decoder profiles match " + target);
            match = &entry;
        }
    }
    return match;
}
std::size_t DecoderRegistry::size() const {
    return entries_.size();
}

namespace detail {
namespace {
class ExtensionInput final : public DecoderInput {
    Archive &a_;
    const Package &package_;
    const ReadOptions &options_;
    std::uint64_t begin_, end_;
    bool failed_ = false;
    template <class F> auto guard(F f) -> decltype(f()) {
        try {
            return f();
        } catch (...) {
            failed_ = true;
            throw;
        }
    }

  public:
    ExtensionInput(Archive &a, const Package &p, const ReadOptions &o, std::uint64_t end)
        : a_(a), package_(p), options_(o), begin_(a.tell()), end_(end) {}
    bool failed() const {
        return failed_;
    }
    std::uint64_t position() const override {
        return a_.tell() - begin_;
    }
    std::uint64_t remaining() const override {
        return end_ - a_.tell();
    }
    void read(void *destination, std::size_t bytes) override {
        guard([&] {
            if (bytes && !destination)
                throw Error("decoder-input", "Null read destination", a_.tell());
            a_.read(destination, bytes);
        });
    }
    void skip(std::uint64_t bytes) override {
        guard([&] { a_.skip(bytes); });
    }
    std::uint32_t count(std::uint32_t limit) override {
        return guard([&] {
            auto maximum = limit ? std::min(limit, options_.max_properties) : options_.max_properties;
            auto count = a_.get<std::int32_t>();
            if (count < 0 || static_cast<std::uint32_t>(count) > maximum)
                throw Error("limit", "Invalid or excessive decoder count", a_.tell() - 4);
            return static_cast<std::uint32_t>(count);
        });
    }
    std::string string() override {
        return guard([&] { return a_.string(); });
    }
    std::string name() override {
        return guard([&] {
            auto index = a_.get<std::int32_t>(), number = a_.get<std::int32_t>();
            if (index < 0 || static_cast<std::size_t>(index) >= package_.names.size() || number < 0)
                throw Error("name-index", "Invalid extension FName", a_.tell() - 8);
            return package_.names[index] + (number ? "_" + std::to_string(number - 1) : "");
        });
    }
    std::string reference() override {
        return guard([&] { return package_.reference(a_.get<std::int32_t>()); });
    }
    std::string guid() override {
        return guard([&] { return a_.guid(); });
    }
};
void validate_json(const Json &value, const ReadOptions &options, std::uint64_t &nodes, std::uint64_t &bytes,
                   unsigned depth = 0) {
    if (depth > options.max_depth || ++nodes > options.max_properties)
        throw Error("decoder-limit", "Decoder result exceeds node/depth limit");
    auto text = [&](const std::string &s) {
        if (s.size() > options.max_string_bytes || s.size() > options.max_property_bytes - bytes)
            throw Error("decoder-limit", "Decoder result exceeds text limit");
        bytes += s.size();
    };
    if (value.is_string())
        text(value.get_ref<const std::string &>());
    else if (value.is_number_float() && !std::isfinite(value.get<double>()))
        throw Error("decoder-json", "Non-finite decoder result; use an explicit JSON representation");
    else if (value.is_binary() || value.is_discarded())
        throw Error("decoder-json", "Decoder must return ordinary JSON values");
    else if (value.is_structured())
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (value.is_object())
                text(it.key());
            validate_json(it.value(), options, nodes, bytes, depth + 1);
        }
}
} // namespace
Json decode_extension(Archive &a, const Package &package, const ObjectExport &object,
                      const ReadOptions &options, const AssetDecoder &decoder, std::uint64_t end,
                      bool exact_size) {
    if (end < a.tell() || end - a.tell() > options.max_property_bytes)
        throw Error("decoder-limit", "Decoder input exceeds byte limit", a.tell());
    Archive::ReadLimit limit(a, end);
    ExtensionInput input(a, package, options, end);
    try {
        auto value = decoder.decode(input, {package.summary, object, exact_size});
        if (input.failed())
            throw Error("decoder-input", "Decoder suppressed an input error", a.tell());
        if ((exact_size && input.remaining()) || (!exact_size && !input.position()))
            throw Error("decoder-consumption", "Decoder did not consume its required payload", a.tell());
        std::uint64_t nodes = 0, bytes = 0;
        validate_json(value, options, nodes, bytes);
        if (value.dump().size() > options.max_property_bytes)
            throw Error("decoder-limit", "Decoder JSON exceeds byte limit", a.tell());
        return {{"decoder", decoder.id}, {"data", std::move(value)}};
    } catch (const Error &) {
        throw;
    } catch (const Json::exception &) {
        throw Error("decoder-json", "Invalid decoder JSON", a.tell());
    } catch (...) {
        throw Error("decoder-exception", "Host decoder threw an exception", a.tell());
    }
}
} // namespace detail
} // namespace cyuediff
