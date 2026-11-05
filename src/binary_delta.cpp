#include "binary_delta_internal.hpp"
#include <algorithm>
#include "binary_delta_plan.hpp"
namespace cyuediff {
namespace {
void require(bool good, const char *message) {
    if (!good)
        throw Error("binary-delta", message);
}
bool sha(const std::string &s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
} // namespace
namespace detail {
void binary_delta_options(const BinaryDeltaOptions &o) {
    require(o.block_size >= 16 && o.block_size <= 1024 * 1024,
            "Block size must be between 16 bytes and 1 MiB");
    require(o.max_input_bytes && o.max_match_bytes && o.max_manifest_bytes && o.max_blocks &&
                o.max_operations && o.max_composition_fragments && o.max_source_read_bytes,
            "Binary delta limits must be nonzero");
    o.control.check();
}
Json binary_delta_validate(const BinaryDelta &d, const BinaryDeltaOptions &o) {
    binary_delta_options(o);
    require(d.block_size >= 16 && d.block_size <= 1024 * 1024 && sha(d.base_sha256) && sha(d.target_sha256),
            "Invalid binary delta identity or block size");
    require(d.base_bytes <= o.max_input_bytes && d.target_bytes <= o.max_input_bytes &&
                d.operations.size() <= o.max_operations,
            "Binary delta input/operation limit exceeded");
    std::uint64_t copied = 0, literals = 0, copy_ops = 0, literal_ops = 0, total = 0;
    for (const auto &op : d.operations) {
        o.control.check();
        require(op.bytes && op.bytes <= d.target_bytes - total, "Invalid delta output extent");
        total += op.bytes;
        if (op.kind == BinaryDeltaKind::copy) {
            require(op.data.empty() && op.offset <= d.base_bytes && op.bytes <= d.base_bytes - op.offset,
                    "Copy exceeds original base");
            copied += op.bytes;
            ++copy_ops;
        } else {
            require(op.kind == BinaryDeltaKind::literal && !op.offset && op.bytes == op.data.size(),
                    "Invalid literal operation");
            require(op.bytes <= o.max_literal_bytes - literals, "Literal byte budget exceeded");
            literals += op.bytes;
            ++literal_ops;
        }
    }
    require(total == d.target_bytes, "Delta operations do not cover the exact target size");
    return {{"schema", "cyuediff.binary-delta-summary/1"},
            {"baseBytes", d.base_bytes},
            {"targetBytes", d.target_bytes},
            {"baseSha256", d.base_sha256},
            {"targetSha256", d.target_sha256},
            {"identical", d.base_bytes == d.target_bytes && d.base_sha256 == d.target_sha256},
            {"blockSize", d.block_size},
            {"copyOperations", copy_ops},
            {"literalOperations", literal_ops},
            {"copiedBytes", copied},
            {"literalBytes", literals}};
}
} // namespace detail
Json BinaryDelta::summary(const BinaryDeltaOptions &o) const {
    return detail::binary_delta_validate(*this, o);
}
namespace {
class MemoryDeltaSource {
    const std::vector<std::uint8_t> &bytes_;
    const OperationControl &control_;

  public:
    MemoryDeltaSource(const std::vector<std::uint8_t> &b, const OperationControl &c)
        : bytes_(b), control_(c) {}
    std::uint64_t size() const {
        return bytes_.size();
    }
    bool empty() const {
        return bytes_.empty();
    }
    std::uint8_t operator[](std::uint64_t at) const {
        return bytes_[static_cast<std::size_t>(at)];
    }
    auto view(std::uint64_t at) const {
        const auto offset = static_cast<std::size_t>(at);
        return std::make_pair(bytes_.data() + offset,
                              std::min<std::size_t>(bytes_.size() - offset, 1024 * 1024));
    }
    std::string hash() const {
        return sha256_bytes(bytes_, control_);
    }
};
} // namespace
BinaryDelta make_binary_delta(const std::vector<std::uint8_t> &base, const std::vector<std::uint8_t> &target,
                              const BinaryDeltaOptions &o) {
    MemoryDeltaSource a(base, o.control), b(target, o.control);
    return detail::plan_binary_delta(a, b, o);
}

} // namespace cyuediff
