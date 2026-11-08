#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
enum class MorphCompressionProfile { unspecified, unreal_5_3_windows };
struct CompressedMorphOptions {
    MorphCompressionProfile profile = MorphCompressionProfile::unspecified;
    std::uint32_t max_vertices = 1'000'000, max_morphs = 4096, max_batches = 1'000'000;
    std::uint64_t max_deltas = 4'000'000, max_data_bytes = 256ull * 1024 * 1024;
    std::uint64_t max_output_bytes = 256ull * 1024 * 1024;
    OperationControl control;
};
struct CompressedMorphDelta {
    std::uint32_t vertex = 0;
    std::array<std::int32_t, 3> quantized_position{}, quantized_normal{};
    std::array<float, 3> position{}, normal{};
    bool normal_stored = false;
    Json to_json() const;
};
// Immutable stored render deltas. No morph names, blend weights, skinning or
// normal recomputation are inferred by this compression primitive.
class CompressedMorphBuffer {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit CompressedMorphBuffer(std::shared_ptr<const Impl>);

  public:
    // Parse exactly one native FMorphTargetVertexInfoBuffers serialization.
    static CompressedMorphBuffer parse(const std::vector<std::uint8_t> &, std::uint32_t vertices,
                                       const CompressedMorphOptions &);
    // Bound all reads to [offset,end); consumed_bytes() identifies the exact
    // window. Parsed words/metadata are owned, independent of caller storage.
    static CompressedMorphBuffer from_native_window(const std::vector<std::uint8_t> &, std::uint64_t offset,
                                                    std::uint64_t end, std::uint32_t vertices,
                                                    const CompressedMorphOptions &);
    std::size_t morph_count() const;
    std::size_t batch_count() const;
    std::uint64_t delta_count(std::size_t morph) const;
    std::uint64_t consumed_bytes() const;
    std::uint64_t accounted_output_bytes() const;
    float position_precision() const;
    float normal_precision() const;
    Json information(const OperationControl & = {}) const;
    Json morph_information(std::size_t, const OperationControl & = {}) const;
    Json batch_information(std::size_t, const OperationControl & = {}) const;
    CompressedMorphDelta delta(std::size_t morph, std::uint64_t entry, const OperationControl & = {}) const;
};
} // namespace cyuediff
