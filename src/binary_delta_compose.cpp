#include "binary_delta_internal.hpp"
#include <algorithm>
namespace cyuediff {
BinaryDelta compose_binary_deltas(const BinaryDelta &first, const BinaryDelta &second,
                                  const BinaryDeltaOptions &o) {
    detail::binary_delta_validate(first, o);
    detail::binary_delta_validate(second, o);
    auto require = [](bool good, const char *message) {
        if (!good)
            throw Error("binary-delta", message);
    };
    require(first.target_bytes == second.base_bytes && first.target_sha256 == second.base_sha256,
            "Intermediate delta identities do not match");
    BinaryDelta result;
    result.base_bytes = first.base_bytes;
    result.base_sha256 = first.base_sha256;
    result.target_bytes = second.target_bytes;
    result.target_sha256 = second.target_sha256;
    result.block_size = first.block_size;
    std::vector<std::uint64_t> ends;
    std::uint64_t end = 0;
    for (const auto &op : first.operations) {
        o.control.check();
        end += op.bytes;
        ends.push_back(end);
    }
    std::size_t fragments = 0;
    std::uint64_t literals = 0, completed = 0;
    auto append = [&](const BinaryDeltaOperation &source, std::uint64_t offset, std::uint64_t bytes) {
        o.control.checkpoint("Composing binary deltas", completed, result.target_bytes);
        require(fragments < o.max_composition_fragments, "Composition fragment budget exceeded");
        ++fragments;
        const bool copy = source.kind == BinaryDeltaKind::copy;
        const auto original = copy ? source.offset + offset : 0;
        const bool merge =
            !result.operations.empty() && result.operations.back().kind == source.kind &&
            (!copy || result.operations.back().offset + result.operations.back().bytes == original);
        if (!copy) {
            require(bytes <= o.max_literal_bytes - literals, "Composed literal byte budget exceeded");
            literals += bytes;
        }
        if (!merge) {
            require(result.operations.size() < o.max_operations, "Composed operation budget exceeded");
            result.operations.push_back({source.kind, original, 0, {}});
        }
        auto &out = result.operations.back();
        if (!copy) {
            require(bytes <= out.data.max_size() - out.data.size(),
                    "Composed literal allocation exceeds address space");
            for (std::uint64_t at = 0; at < bytes;) {
                o.control.check();
                const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(bytes - at, 1024 * 1024));
                const auto begin = source.data.begin() + static_cast<std::size_t>(offset + at);
                out.data.insert(out.data.end(), begin, begin + n);
                at += n;
            }
        }
        out.bytes += bytes;
        completed += bytes;
    };
    for (const auto &op : second.operations) {
        if (op.kind == BinaryDeltaKind::literal)
            append(op, 0, op.bytes);
        else {
            auto at = op.offset, remaining = op.bytes;
            auto index =
                static_cast<std::size_t>(std::upper_bound(ends.begin(), ends.end(), at) - ends.begin());
            while (remaining) {
                const auto begin = index ? ends[index - 1] : 0;
                const auto n = std::min(remaining, ends[index] - at);
                append(first.operations[index], at - begin, n);
                remaining -= n;
                at += n;
                ++index;
            }
        }
    }
    result.summary(o);
    o.control.checkpoint("Composing binary deltas", result.target_bytes, result.target_bytes);
    return result;
}
} // namespace cyuediff
