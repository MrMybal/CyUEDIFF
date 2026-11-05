#pragma once
#include "animation_source_model.hpp"
#include "skeleton_model.hpp"
namespace cyuediff::test {
inline Package hierarchy_animation_model() {
    auto p = animation_source_model();
    auto &tracks = p.exports[0].properties["BoneAnimationTracks"]["value"];
    tracks[0]["InternalTrackData"]["value"]["positions"] = {{0., 0., 0.}, {3., 4., 0.}, {6., 8., 0.}};
    tracks[1]["Name"]["value"] = "arm";
    tracks[1]["InternalTrackData"]["value"]["positions"] = {{10., 0., 0.}};
    auto tip = tracks[1];
    tip["Name"]["value"] = "tip";
    tip["BoneTreeIndex"]["value"] = 2;
    tip["InternalTrackData"]["value"]["positions"] = {{0., 20., 0.}};
    tracks.push_back(std::move(tip));
    return p;
}
inline Package hierarchy_skeleton_model() {
    auto p = skeleton_model();
    p.complete_input = true;
    p.exports[0].native_data["bones"][2]["parentIndex"] = 1;
    return p;
}
} // namespace cyuediff::test
