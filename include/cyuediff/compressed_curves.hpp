#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
enum class CompressedCurveProfile { unspecified, unreal_5_3_windows };
enum class CompressedCurveCodec { unspecified, rich_curve, uniform_indexable, uniformly_sampled };
struct CompressedCurveSource {
    CompressedCurveCodec codec = CompressedCurveCodec::unspecified;
    // Names in encoded curve order, not a process-local FName sorting permutation.
    std::vector<std::string> names;
    std::vector<std::uint8_t> buffer;
};
struct CompressedCurveOptions {
    CompressedCurveProfile profile = CompressedCurveProfile::unspecified;
    std::uint32_t max_curves = 100000;
    std::uint64_t max_keys = 4000000, max_read_bytes = 128ull * 1024 * 1024,
                  max_output_bytes = 256ull * 1024 * 1024, max_text_bytes = 16ull * 1024 * 1024;
    OperationControl control;
};
class CompressedCurves {
  public:
    struct Impl;
    std::size_t curve_count() const;
    Json catalog(const OperationControl & = {}) const;
    Json to_json(bool include_keys = false, const OperationControl & = {}) const;
    // Original encoded values/tangents. Uniform sample times are identified as derived.
    Json stored_key(std::size_t curve, std::uint32_t key, const OperationControl & = {}) const;
    Json key_preview(std::size_t curve, std::uint32_t max_points = 2048, const OperationControl & = {}) const;
    // Encoded-order scalar sampling in seconds. No animation interpolation/retarget/blend defaults.
    // RichCurve follows the explicit native profile; uniform codecs use the stored sample grid.
    double sample_curve(std::size_t curve, double seconds, double empty_default = 0,
                        const OperationControl & = {}) const;
    Json sample(double seconds, double empty_default = 0, const OperationControl & = {}) const;

  private:
    std::shared_ptr<const Impl> impl_;
    explicit CompressedCurves(std::shared_ptr<const Impl>);
    friend CompressedCurves decode_compressed_curves(const CompressedCurveSource &,
                                                     const CompressedCurveOptions &);
};
// The caller must verify the codec class/dependency separately; names alone never select a codec.
CompressedCurves decode_compressed_curves(const CompressedCurveSource &, const CompressedCurveOptions &);
} // namespace cyuediff
