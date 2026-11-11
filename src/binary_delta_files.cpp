#include "binary_delta_internal.hpp"
#include "archive.hpp"
namespace cyuediff {
namespace {
class FileDeltaInput {
    ReadOptions read_;
    fs::path path_;
    std::unique_ptr<detail::Archive> input_;

  public:
    FileDeltaInput(const std::optional<fs::path> &path, const BinaryDeltaOptions &o) {
        if (!path)
            return;
        path_ = *path;
        if (!fs::is_regular_file(path_))
            throw Error("binary-delta", "Binary delta inputs must be regular files");
        read_.control = o.control;
        input_ = std::make_unique<detail::Archive>(path_, read_);
        if (input_->size() > o.max_input_bytes)
            throw Error("binary-delta", "Input byte budget exceeded");
        check();
    }
    BinaryDeltaSource source() {
        if (!input_)
            return {};
        return {input_->size(), [this](std::uint64_t at, std::uint8_t *out, std::size_t n) {
                    // Rereads verify that the file did not change: never serve them from a window.
                    input_->read_at(at, out, n);
                    return n;
                }};
    }
    void check() const {
        if (input_ && fs::file_size(path_) != input_->size())
            throw Error("binary-delta", "Input file size changed during delta generation");
    }
};
} // namespace
BinaryDelta make_binary_delta_file_pair(const std::optional<fs::path> &base,
                                        const std::optional<fs::path> &target, const BinaryDeltaOptions &o) {
    detail::binary_delta_options(o);
    FileDeltaInput a(base, o), b(target, o);
    auto result = make_binary_delta_sources(a.source(), b.source(), o);
    a.check();
    b.check();
    return result;
}
BinaryDelta make_binary_delta_files(const fs::path &base, const fs::path &target,
                                    const BinaryDeltaOptions &o) {
    return make_binary_delta_file_pair(base, target, o);
}
} // namespace cyuediff
