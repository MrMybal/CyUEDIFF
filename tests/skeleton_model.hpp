#pragma once
#include <cyuediff/skeleton.hpp>
namespace cyuediff::test {
inline Package skeleton_model() {
    Package p;
    ObjectExport e;
    e.path = "Rig.Skeleton";
    e.class_name = "/Script/Engine.Skeleton";
    e.coverage = "properties";
    e.native_coverage = "reference-skeleton";
    Json pose = {{"rotation", {0., 0., 0., 1.}}, {"translation", {0., 0., 0.}}, {"scale", {1., 1., 1.}}};
    auto arm = pose, tip = pose;
    arm["translation"] = {10., 0., 0.};
    tip["translation"] = {0., 20., 0.};
    e.native_data = {{"kind", "reference-skeleton"},
                     {"layout", "host-test"},
                     {"bones", Json::array({{{"name", "root"}, {"parentIndex", -1}},
                                            {{"name", "arm"}, {"parentIndex", 0}},
                                            {{"name", "tip"}, {"parentIndex", 0}}})},
                     {"localPoses", Json::array({pose, arm, tip})},
                     {"nameToIndex", {{"root", 0}, {"arm", 1}, {"tip", 2}}}};
    p.exports.push_back(e);
    return p;
}
} // namespace cyuediff::test
