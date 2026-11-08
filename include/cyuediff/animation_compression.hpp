#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
// This is an explicit wire profile, not a codec inferred from an instance name.
enum class PerTrackAnimationProfile { unspecified, unreal_5_3_windows };
enum class CompressedAnimationInterpolation { linear, step };
enum class CompressedAnimationChannel { translation, rotation, scale };
struct PerTrackAnimationSource {
    std::uint32_t frame_count = 0, track_offset_count = 0, scale_offset_count = 0;
    std::uint32_t scale_strip_size = 2, key_byte_count = 0;
    std::vector<std::int32_t> track_to_skeleton;
    // FUECompressedAnimData's complete buffer: track offsets, scale offsets, key bytes.
    std::vector<std::uint8_t> buffer;
};
struct PerTrackAnimationOptions {
    PerTrackAnimationProfile profile = PerTrackAnimationProfile::unspecified;
    std::uint32_t max_tracks = 100000, max_frames = 1000000;
    std::uint64_t max_keys = 4000000, max_read_bytes = 256ull * 1024 * 1024;
    std::uint64_t max_output_bytes = 256ull * 1024 * 1024;
    OperationControl control;
};
struct CompressedAnimationBonePose {
    std::uint32_t track = 0;
    std::int32_t skeleton_index = 0;
    std::array<double, 3> translation{}, scale{1, 1, 1};
    std::array<double, 4> rotation{0, 0, 0, 1};
    Json to_json() const;
};
class PerTrackAnimation {
  public:
    struct Impl;
    std::uint32_t frame_count() const;
    std::size_t track_count() const;
    // Includes exact buffer hashes, source offsets, formats and unreferenced byte count.
    // include_keys exposes decoded stored keys, not resampled or retargeted poses.
    Json to_json(bool include_keys = false) const;
    // Bounded metadata without large frame tables or key arrays.
    Json catalog(const OperationControl & = {}) const;
    // Original key indices/endpoints retained; omitted points are explicitly counted.
    Json key_preview(std::size_t track, std::uint32_t max_points = 2048, const OperationControl & = {}) const;
    Json stored_key(std::size_t track, CompressedAnimationChannel, std::uint32_t key,
                    const OperationControl & = {}) const;
    CompressedAnimationBonePose
    sample_track(std::size_t track, double frame,
                 CompressedAnimationInterpolation = CompressedAnimationInterpolation::linear,
                 const OperationControl & = {}) const;
    // Explicit frame domain [0, frame_count - 1]. No looping, retargeting,
    // additive-base application, Skeleton defaults, curves or gameplay evaluation.
    std::vector<CompressedAnimationBonePose>
    sample(double frame, CompressedAnimationInterpolation = CompressedAnimationInterpolation::linear,
           const OperationControl & = {}) const;

  private:
    std::shared_ptr<const Impl> impl_;
    Json json(bool keys, bool timing, const OperationControl & = {}) const;
    explicit PerTrackAnimation(std::shared_ptr<const Impl>);
    friend PerTrackAnimation decode_per_track_animation(const PerTrackAnimationSource &,
                                                        const PerTrackAnimationOptions &);
};
// Decodes only the explicitly selected UE 5.3 per-track format. A package reader
// must independently verify the selected codec class and serialized view metadata.
// Bone indices are retained; names require an independently verified Skeleton.
PerTrackAnimation decode_per_track_animation(const PerTrackAnimationSource &,
                                             const PerTrackAnimationOptions &);
} // namespace cyuediff
