#include "binary_delta_internal.hpp"
#include "archive.hpp"
#include "sha256.hpp"
#include <algorithm>
namespace cyuediff {
void apply_binary_delta_file(const fs::path &base, const BinaryDelta &d, const BinaryDeltaSink &sink,
                             const BinaryDeltaOptions &o) {
    detail::binary_delta_validate(d, o);
    auto require = [](bool good, const char *message) {
        if (!good)
            throw Error("binary-delta", message);
    };
    require(bool(sink), "A reconstruction sink is required");
    ReadOptions read;
    read.control = o.control;
    detail::Archive input(base, read);
    require(input.size() == d.base_bytes, "Base file size does not match delta identity");
    require(input.hash(0, input.size()) == d.base_sha256, "Base file SHA-256 does not match delta identity");
    require(fs::file_size(base) == d.base_bytes, "Base file size changed during verification");
    detail::Sha256 hash;
    std::vector<std::uint8_t> buffer(1024 * 1024);
    std::uint64_t completed = 0;
    for (const auto &op : d.operations) {
        if (op.kind == BinaryDeltaKind::copy)
            input.seek(op.offset);
        for (std::uint64_t at = 0; at < op.bytes;) {
            o.control.checkpoint("Streaming binary target", completed, d.target_bytes);
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(op.bytes - at, buffer.size()));
            const auto *bytes = op.kind == BinaryDeltaKind::copy
                                    ? buffer.data()
                                    : op.data.data() + static_cast<std::size_t>(at);
            if (op.kind == BinaryDeltaKind::copy)
                input.read(buffer.data(), n);
            hash.update(bytes, n);
            sink(bytes, n);
            at += n;
            completed += n;
        }
    }
    require(fs::file_size(base) == d.base_bytes, "Base file size changed during reconstruction");
    require(hash.finish() == d.target_sha256, "Streamed target SHA-256 does not match delta identity");
    o.control.checkpoint("Streaming binary target", completed, d.target_bytes);
}
} // namespace cyuediff
