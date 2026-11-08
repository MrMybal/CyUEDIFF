#pragma once
#include <cyuediff/animation_compression.hpp>
#include <cyuediff/animation.hpp>
#include <cyuediff/zen.hpp>
#include <optional>
namespace cyuediff {
enum class CookedAnimationProfile { unspecified, unreal_5_3_windows };
// Explicit host-selected dependency source; copied Container handles share read-only state.
// Its package name/public export hash must match the animation's stored reference.
struct CookedAnimationDependency {
    Container container;
    std::size_t entry = SIZE_MAX;
    ZenReadOptions zen;
    ContainerReadOptions read;
};
struct CookedAnimationOptions {
    CookedAnimationProfile profile = CookedAnimationProfile::unspecified;
    std::optional<CookedAnimationDependency> bone_settings, skeleton;
    // Required for sampling only if Interpolation is omitted from mapped properties.
    // A conflicting stored value is refused. This is not a reconstructed default.
    std::optional<CompressedAnimationInterpolation> interpolation;
    PerTrackAnimationOptions channels;
    std::uint64_t max_read_bytes = 512ull * 1024 * 1024, max_output_bytes = 256ull * 1024 * 1024;
    std::uint32_t max_bones = 65536, max_curves = 100000, max_retarget_sources = 1024;
    std::uint32_t max_codec_exports = 16384, max_outer_depth = 128, max_string_bytes = 65536;
    NarrowStringEncoding narrow_string_encoding = NarrowStringEncoding::unspecified;
    OperationControl control;
};
class CookedAnimation {
  public:
    struct Impl;
    bool decoded() const;
    bool skeleton_bound() const;
    std::optional<std::uint32_t> frame_count() const;
    Json to_json(bool include_keys = false) const;
    // Lightweight immutable-source catalogue, without full timing/key/pose arrays.
    Json catalog(const OperationControl & = {}) const;
    Json key_preview(std::size_t track, std::uint32_t max_points = 2048, const OperationControl & = {}) const;
    Json stored_key(std::size_t track, CompressedAnimationChannel, std::uint32_t key,
                    const OperationControl & = {}) const;
    Json sample_track(std::size_t track, double frame, const OperationControl & = {}) const;
    // Stored compressed bone-local samples. Names require a verified Skeleton;
    // absent dependencies/interpolation yield explicit unavailable coverage.
    // No retargeting, additive-base application, missing-track fallback or skinning.
    Json sample(double frame, const OperationControl & = {}) const;

  private:
    std::shared_ptr<const Impl> impl_;
    explicit CookedAnimation(std::shared_ptr<const Impl>);
    friend CookedAnimation read_zen_animation(const Container &, std::size_t, const ZenReadOptions &,
                                              const ContainerReadOptions &, const CookedAnimationOptions &,
                                              std::size_t);
};
CookedAnimation read_zen_animation(const Container &, std::size_t package_entry, const ZenReadOptions &,
                                   const ContainerReadOptions &, const CookedAnimationOptions &,
                                   std::size_t export_index = SIZE_MAX);
// Exact verified bone names pair independently selected fractional frames.
// Unknown peer data/binding cannot become a known addition/removal.
Json compare_cooked_animation_frames(const CookedAnimation &, const CookedAnimation &, double before_frame,
                                     double after_frame, const AnimationFrameDiffOptions & = {});
} // namespace cyuediff
