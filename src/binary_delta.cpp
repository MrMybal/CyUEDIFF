#include "binary_delta_internal.hpp"
#include <algorithm>
#include <unordered_map>
namespace cyuediff {
namespace {
void require(bool good, const char *message) {
    if (!good)
        throw Error("binary-delta", message);
}
bool sha(const std::string &s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::uint64_t window_hash(const std::vector<std::uint8_t> &v, std::size_t at, std::size_t count,
                          const OperationControl &control) {
    std::uint64_t h = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!(i & 65535))
            control.check();
        h = h * 257 + v[at + i];
    }
    return h;
}
} // namespace
namespace detail {
void binary_delta_options(const BinaryDeltaOptions &o) {
    require(o.block_size >= 16 && o.block_size <= 1024 * 1024,
            "Block size must be between 16 bytes and 1 MiB");
    require(o.max_input_bytes && o.max_match_bytes && o.max_manifest_bytes && o.max_blocks &&
                o.max_operations,
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
BinaryDelta make_binary_delta(const std::vector<std::uint8_t> &base, const std::vector<std::uint8_t> &target,
                              const BinaryDeltaOptions &o) {
    detail::binary_delta_options(o);
    require(base.size() <= o.max_input_bytes && target.size() <= o.max_input_bytes,
            "Input byte budget exceeded");
    BinaryDelta d;
    d.base_bytes = base.size();
    d.target_bytes = target.size();
    d.block_size = o.block_size;
    d.base_sha256 = sha256_bytes(base, o.control);
    d.target_sha256 = sha256_bytes(target, o.control);
    std::uint64_t literal_bytes = 0;
    auto reserve_operation = [&] {
        require(d.operations.size() < o.max_operations, "Delta operation budget exceeded");
    };
    auto literal = [&](std::size_t at, std::size_t count) {
        if (!count)
            return;
        require(count <= o.max_literal_bytes - literal_bytes, "Literal byte budget exceeded");
        if (d.operations.empty() || d.operations.back().kind != BinaryDeltaKind::literal) {
            reserve_operation();
            d.operations.push_back({BinaryDeltaKind::literal, 0, 0, {}});
        }
        auto &op = d.operations.back();
        for (std::size_t i = 0; i < count; ++i) {
            if (!(i & 65535))
                o.control.check();
            op.data.push_back(target[at + i]);
        }
        op.bytes += count;
        literal_bytes += count;
    };
    auto copy = [&](std::size_t at, std::size_t count) {
        if (!d.operations.empty() && d.operations.back().kind == BinaryDeltaKind::copy &&
            d.operations.back().offset + d.operations.back().bytes == at) {
            d.operations.back().bytes += count;
        } else {
            reserve_operation();
            d.operations.push_back({BinaryDeltaKind::copy, at, count, {}});
        }
    };
    // Preserve small identical files too; hashes are confirmed with direct byte comparison.
    if (base == target) {
        if (!base.empty())
            copy(0, base.size());
        d.summary(o);
        return d;
    }
    const std::size_t block = o.block_size, count = base.size() / block;
    require(count <= o.max_blocks, "Base block index budget exceeded");
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> index;
    for (std::size_t i = 0; i < count; ++i) {
        o.control.checkpoint("Indexing binary base blocks", i, count);
        index[window_hash(base, i * block, block, o.control)].push_back(i * block);
    }
    std::uint64_t power = 1, matched_work = 0;
    for (std::size_t i = 1; i < block; ++i)
        power *= 257;
    auto equal_byte = [&](std::size_t a, std::size_t b) {
        require(matched_work < o.max_match_bytes, "Candidate comparison work budget exceeded");
        if (!(matched_work++ & 65535))
            o.control.check();
        return base[a] == target[b];
    };
    std::size_t at = 0, pending = 0;
    std::uint64_t h = 0;
    bool hash_ready = false;
    while (at <= target.size() && block <= target.size() - at && !index.empty()) {
        if (!(at & 4095))
            o.control.checkpoint("Matching binary target blocks", at, target.size());
        if (!hash_ready)
            h = window_hash(target, at, block, o.control);
        hash_ready = true;
        const auto found = index.find(h);
        std::size_t source = 0, length = 0;
        if (found != index.end()) {
            for (const auto candidate : found->second) {
                std::size_t n = 0;
                while (n < block && equal_byte(candidate + n, at + n))
                    ++n;
                if (n == block) {
                    source = candidate;
                    length = n;
                    break;
                }
            }
        }
        if (length) {
            while (length < base.size() - source && length < target.size() - at &&
                   equal_byte(source + length, at + length))
                ++length;
            literal(pending, at - pending);
            copy(source, length);
            at += length;
            pending = at;
            hash_ready = false;
        } else {
            if (block < target.size() - at)
                h = (h - std::uint64_t(target[at]) * power) * 257 + target[at + block];
            ++at;
        }
    }
    literal(pending, target.size() - pending);
    d.summary(o);
    o.control.checkpoint("Matching binary target blocks", target.size(), target.size());
    return d;
}
} // namespace cyuediff
