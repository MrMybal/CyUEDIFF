#pragma once
#include "binary_delta_internal.hpp"
#include <algorithm>
#include <cstring>
#include <unordered_map>
namespace cyuediff::detail {
inline void binary_plan_require(bool good, const char *message) {
    if (!good)
        throw Error("binary-delta", message);
}
template <class A, class B> bool binary_sources_equal(A &a, B &b, const OperationControl &control) {
    if (a.size() != b.size())
        return false;
    for (std::uint64_t at = 0; at < a.size();) {
        control.check();
        const auto left = a.view(at), right = b.view(at);
        const auto n = std::min(left.second, right.second);
        if (std::memcmp(left.first, right.first, n))
            return false;
        at += n;
    }
    return true;
}
template <class Source>
std::uint64_t binary_window_hash(Source &v, std::uint64_t at, std::uint64_t count,
                                 const OperationControl &control) {
    std::uint64_t h = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (!(i & 65535))
            control.check();
        h = h * 257 + v[at + i];
    }
    return h;
}
template <class A, class B> BinaryDelta plan_binary_delta(A &base, B &target, const BinaryDeltaOptions &o) {

    detail::binary_delta_options(o);
    binary_plan_require(base.size() <= o.max_input_bytes && target.size() <= o.max_input_bytes,
                        "Input byte budget exceeded");
    BinaryDelta d;
    d.base_bytes = base.size();
    d.target_bytes = target.size();
    d.block_size = o.block_size;
    d.base_sha256 = base.hash();
    d.target_sha256 = target.hash();
    std::uint64_t literal_bytes = 0;
    auto reserve_operation = [&] {
        binary_plan_require(d.operations.size() < o.max_operations, "Delta operation budget exceeded");
    };
    auto literal = [&](std::uint64_t at, std::uint64_t count) {
        if (!count)
            return;
        binary_plan_require(count <= o.max_literal_bytes - literal_bytes, "Literal byte budget exceeded");
        if (d.operations.empty() || d.operations.back().kind != BinaryDeltaKind::literal) {
            reserve_operation();
            d.operations.push_back({BinaryDeltaKind::literal, 0, 0, {}});
        }
        auto &op = d.operations.back();
        binary_plan_require(count <= op.data.max_size() - op.data.size(),
                            "Literal allocation exceeds address space");
        for (std::uint64_t done = 0; done < count;) {
            o.control.check();
            const auto view = target.view(at + done);
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(count - done, view.second));
            op.data.insert(op.data.end(), view.first, view.first + n);
            done += n;
        }
        op.bytes += count;
        literal_bytes += count;
    };
    auto copy = [&](std::uint64_t at, std::uint64_t count) {
        if (!d.operations.empty() && d.operations.back().kind == BinaryDeltaKind::copy &&
            d.operations.back().offset + d.operations.back().bytes == at) {
            d.operations.back().bytes += count;
        } else {
            reserve_operation();
            d.operations.push_back({BinaryDeltaKind::copy, at, count, {}});
        }
    };
    // Preserve small identical files too; hashes are confirmed with direct byte comparison.
    if (target.empty() || binary_sources_equal(base, target, o.control)) {
        if (!target.empty())
            copy(0, base.size());
        d.summary(o);
        return d;
    }
    const std::uint64_t block = o.block_size, count = base.size() / block;
    binary_plan_require(count <= o.max_blocks, "Base block index budget exceeded");
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> index;
    for (std::uint64_t i = 0; i < count; ++i) {
        o.control.checkpoint("Indexing binary base blocks", i, count);
        index[binary_window_hash(base, i * block, block, o.control)].push_back(i * block);
    }
    std::uint64_t power = 1, matched_work = 0;
    for (std::uint64_t i = 1; i < block; ++i)
        power *= 257;
    auto equal_byte = [&](std::uint64_t a, std::uint64_t b) {
        binary_plan_require(matched_work < o.max_match_bytes, "Candidate comparison work budget exceeded");
        if (!(matched_work++ & 65535))
            o.control.check();
        return base[a] == target[b];
    };
    std::uint64_t at = 0, pending = 0;
    std::uint64_t h = 0;
    bool hash_ready = false;
    while (at <= target.size() && block <= target.size() - at && !index.empty()) {
        if (!(at & 4095))
            o.control.checkpoint("Matching binary target blocks", at, target.size());
        if (!hash_ready)
            h = binary_window_hash(target, at, block, o.control);
        hash_ready = true;
        const auto found = index.find(h);
        std::uint64_t source = 0, length = 0;
        if (found != index.end()) {
            for (const auto candidate : found->second) {
                std::uint64_t n = 0;
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
} // namespace cyuediff::detail
