#pragma once
#include <cyuediff/core.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace cyuediff {
struct VersionRange {
    int min = std::numeric_limits<int>::min(), max = std::numeric_limits<int>::max();
    bool contains(int value) const;
};
struct SerializationProfile {
    VersionRange ue4, ue5;
    // Required GUIDs: 32 lowercase hexadecimal digits, as in PackageSummary::custom_versions.
    std::map<std::string, VersionRange> custom_versions;
    bool matches(const PackageSummary &) const;
};
enum class DecoderKind { struct_value, native_export };
enum class DecoderPrefix { none, tagged_properties };
struct TaggedType;
struct DecoderContext {
    const PackageSummary &summary;
    const ObjectExport &object;
    // For array elements the window ends at the enclosing property's end, not the element's end.
    bool exact_size;
    // Shared enclosing work/text budget; include failed reads. Optional for standalone hosts.
    std::function<void(std::uint32_t, std::uint64_t)> charge;
    std::uint32_t max_depth = UINT32_MAX;
    // Borrowed for this callback only. Present for a certified tagged-prefix native tail.
    const Json *tagged_properties = nullptr;
    // Borrowed for this callback only; nullptr for a native export. Includes effective owner/GUID.
    const TaggedType *effective_type = nullptr;
};
// Sequential, little-endian, borrowed for the callback duration. No seek or whole-file access.
class DecoderInput {
  protected:
    DecoderInput() = default;

  public:
    virtual ~DecoderInput() = default;
    DecoderInput(const DecoderInput &) = delete;
    DecoderInput &operator=(const DecoderInput &) = delete;
    virtual std::uint64_t position() const = 0;
    virtual std::uint64_t remaining() const = 0;
    virtual void read(void *destination, std::size_t bytes) = 0;
    virtual void skip(std::uint64_t bytes) = 0;
    virtual std::uint32_t count(std::uint32_t limit = 0) = 0;
    virtual std::string string() = 0;
    virtual std::string name() = 0;
    virtual std::string reference() = 0;
    virtual std::string guid() = 0;
    template <class T> T get() {
        static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && sizeof(T) <= 8,
                      "Use fixed-width integers, float or double; use uint8_t for a boolean");
        std::array<unsigned char, sizeof(T)> bytes{};
        read(bytes.data(), bytes.size());
        const std::uint16_t endian = 1;
        if (*reinterpret_cast<const unsigned char *>(&endian) != 1)
            std::reverse(bytes.begin(), bytes.end());
        T result;
        std::memcpy(&result, bytes.data(), sizeof(T));
        return result;
    }
};
using DecoderCallback = std::function<Json(DecoderInput &, const DecoderContext &)>;
struct AssetDecoder {
    // Stable, versioned schema identity, included with each decoded value in snapshots.
    std::string id;
    DecoderKind kind = DecoderKind::struct_value;
    // Exact serialized struct name or full resolved export class path. No inheritance/wildcards.
    std::string target;
    SerializationProfile profile;
    DecoderCallback decode;
    // Struct callbacks read the native tail after an explicitly certified tagged prefix.
    DecoderPrefix prefix = DecoderPrefix::none;
    // Optional canonical SHA-256 of this implementation/schema, separate from stored values.
    // Hosts remain responsible for stable versioned IDs and honest callback/state identities.
    std::string definition_sha256;
    // Optional exact canonical struct GUID selector; valid only for struct_value registrations.
    std::string struct_guid;
};
// Immutable after construction. Callbacks and their captured state must support the host's threading.
class TaggedTypes;
class DecoderRegistry {
    const std::vector<AssetDecoder> entries_;
    std::shared_ptr<const TaggedTypes> tagged_types_;
    std::string context_sha256_;
    std::map<std::pair<DecoderKind, std::string>, std::vector<std::size_t>> index_;

  public:
    explicit DecoderRegistry(std::vector<AssetDecoder> entries, std::shared_ptr<const TaggedTypes> = {},
                             std::string context_sha256 = {});
    const std::string &context_sha256() const { return context_sha256_; }
    const std::shared_ptr<const TaggedTypes> &tagged_types() const {
        return tagged_types_;
    }
    // No match returns nullptr; overlapping matches fail instead of depending on registration order.
    const AssetDecoder *find(DecoderKind, const std::string &target, const PackageSummary &,
                             const std::string &struct_guid = {}) const;
    std::size_t size() const;
    // Explicit definition hashes for registrations matching this archive profile.
    std::map<std::string, std::string> definitions(const PackageSummary &,
                                                   const OperationControl & = {}) const;
};
} // namespace cyuediff
