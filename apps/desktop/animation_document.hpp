#pragma once
#include "document.hpp"
namespace cyuediff::desktop {
// Worker-only preparation. No UI/GPU access and no implicit external object discovery.
void prepare_animation_poses(Document &, const LoadRequest &, const ReadOptions &, const ReadOptions &);
Json animation_pose_report(const Document &);
} // namespace cyuediff::desktop
