#pragma once
#include "skeleton_model.hpp"
namespace cyuediff::test {
inline Package animation_model() {
    auto p = retarget_model();
    p.exports[0].native_data["curveMetadata"] = {
        {"coverage", "stored"},
        {"containers",
         {{"AnimationCurves",
           {{"Smile~/curve",
             {{"material", false}, {"morphTarget", true}, {"maxLOD", 2}, {"linkedBones", {"arm", "tip"}}}},
            {"EyeWeight",
             {{"material", true}, {"morphTarget", false}, {"maxLOD", 255}, {"linkedBones", Json::array()}}}}},
          {"Track", Json::object()}}}};
    p.exports[0].native_data["syncMarkers"] = {{"coverage", "stored"},
                                               {"stripFlags", {{"global", 0}, {"class", 0}}},
                                               {"names", {"Left", "Right", "Left"}}};
    return p;
}
} // namespace cyuediff::test
