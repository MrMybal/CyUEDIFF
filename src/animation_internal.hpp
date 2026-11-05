#pragma once
#include <cyuediff/animation.hpp>
namespace cyuediff::detail {
Json sample_animation_track(const Json &track, std::uint32_t frame, std::uint32_t count,
                            const OperationControl & = {});
// Accepts only a validated animation_snapshot model body owned by the caller.
Json sample_animation_frame(const Json &model, std::uint32_t frame, const std::string &path,
                            bool complete_input, const OperationControl & = {});
} // namespace cyuediff::detail
