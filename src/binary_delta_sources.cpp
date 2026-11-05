#include "binary_delta_plan.hpp"
#include "sha256.hpp"
namespace cyuediff {
namespace {
constexpr std::uint64_t window_bytes = 1024 * 1024;
class CachedDeltaSource {
    const BinaryDeltaSource &source_;
    const BinaryDeltaOptions &options_;
    std::uint64_t &read_bytes_;
    struct Window {
        std::uint64_t start = 0;
        bool valid = false;
        std::unique_ptr<std::uint8_t[]> bytes;
        std::size_t count = 0;
    };
    Window windows_[2];
    unsigned next_ = 0;

  public:
    CachedDeltaSource(const BinaryDeltaSource &s, const BinaryDeltaOptions &o, std::uint64_t &work)
        : source_(s), options_(o), read_bytes_(work) {}
    std::uint64_t size() const {
        return source_.bytes;
    }
    bool empty() const {
        return !size();
    }
    std::pair<const std::uint8_t *, std::size_t> view(std::uint64_t at) {
        detail::binary_plan_require(at < size(), "Binary source read exceeds declared extent");
        for (auto &window : windows_)
            if (window.valid && at >= window.start && at - window.start < window.count) {
                const auto offset = static_cast<std::size_t>(at - window.start);
                return {window.bytes.get() + offset, window.count - offset};
            }
        auto &window = windows_[next_];
        next_ ^= 1;
        window.valid = false;
        window.start = at / window_bytes * window_bytes;
        const auto n = static_cast<std::size_t>(std::min(window_bytes, size() - window.start));
        detail::binary_plan_require(n <= options_.max_source_read_bytes - read_bytes_,
                                    "Binary source read byte budget exceeded");
        options_.control.check();
        read_bytes_ += n;
        if (!window.bytes)
            window.bytes =
                std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(std::min(window_bytes, size())));
        window.count = n;
        detail::binary_plan_require(source_.read(window.start, window.bytes.get(), n) == n,
                                    "Binary source callback returned a short or excessive read");
        options_.control.check();
        window.valid = true;
        const auto offset = static_cast<std::size_t>(at - window.start);
        return {window.bytes.get() + offset, n - offset};
    }
    std::uint8_t operator[](std::uint64_t at) {
        return *view(at).first;
    }
    std::string hash() {
        for (auto &window : windows_)
            window.valid = false;
        detail::Sha256 hash;
        for (std::uint64_t at = 0; at < size();) {
            options_.control.checkpoint("Hashing binary source", at, size());
            const auto bytes = view(at);
            hash.update(bytes.first, bytes.second);
            at += bytes.second;
        }
        options_.control.checkpoint("Hashing binary source", size(), size());
        return hash.finish();
    }
};
} // namespace
BinaryDelta make_binary_delta_sources(const BinaryDeltaSource &base, const BinaryDeltaSource &target,
                                      const BinaryDeltaOptions &o) {
    detail::binary_delta_options(o);
    detail::binary_plan_require(base.bytes <= o.max_input_bytes && target.bytes <= o.max_input_bytes,
                                "Input byte budget exceeded");
    detail::binary_plan_require((!base.bytes || bool(base.read)) && (!target.bytes || bool(target.read)),
                                "Nonempty binary sources require read callbacks");
    std::uint64_t read_bytes = 0;
    CachedDeltaSource a(base, o, read_bytes), b(target, o, read_bytes);
    auto result = detail::plan_binary_delta(a, b, o);
    detail::binary_plan_require(a.hash() == result.base_sha256 && b.hash() == result.target_sha256,
                                "Binary source bytes changed during delta generation");
    return result;
}
} // namespace cyuediff
