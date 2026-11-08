#include <limits>
#pragma once
#include <cyuediff/core.hpp>
#include <cmath>
#include <string_view>
namespace cyuediff::view_state {
// Data-only presentation contract. No ImGui, CyUI, resource handles or input paths.
inline void require(bool ok) {
    if (!ok)
        throw Error("view-state", "Invalid or unsupported view state");
}
inline void text(const Json &v) {
    require(v.is_string() && v.get_ref<const std::string &>().size() <= 4096);
    for (unsigned char c : v.get_ref<const std::string &>())
        require(c >= 32 && c != 127);
}
inline void scalar(const Json &v, double low, double high) {
    require(v.is_number());
    const auto n = v.get<double>();
    require(std::isfinite(n) && n >= low && n <= high);
}
inline void vector(const Json &v, std::size_t n) {
    require(v.is_array() && v.size() == n);
    for (const auto &x : v)
        scalar(x, -1e12, 1e12);
}
inline void validate(const Json &v, std::string_view kind = "document");
inline void map(const Json &v, std::string_view kind) {
    require(v.is_object() && v.size() <= 128);
    for (auto it = v.begin(); it != v.end(); ++it) {
        text(it.key());
        validate(it.value(), kind);
    }
}
inline void validate(const Json &v, std::string_view kind) {
    require(v.is_object());
    for (auto it = v.begin(); it != v.end(); ++it) {
        const auto &key = it.key();
        const auto &x = it.value();
        bool known = false;
        auto flag = [&](std::initializer_list<const char *> names) {
            for (auto name : names)
                if (key == name) {
                    require(x.is_boolean());
                    known = true;
                }
        };
        auto string = [&](std::initializer_list<const char *> names, bool nullable = false) {
            for (auto name : names)
                if (key == name) {
                    if (!nullable || !x.is_null())
                        text(x);
                    known = true;
                }
        };
        auto child = [&](const char *name, std::string_view type) {
            if (key == name) {
                validate(x, type);
                known = true;
            }
        };
        auto entries = [&](const char *name, std::string_view type) {
            if (key == name) {
                map(x, type);
                known = true;
            }
        };
        auto columns = [&](const char *name, std::size_t count) {
            if (key == name) {
                require(x.is_array() && x.size() == count);
                for (const auto &weight : x)
                    scalar(weight, .0001, 1000);
                known = true;
            }
        };
        if (kind == "shell-layout") {
            if (key == "sidebarWidth") {
                scalar(x, 180, 640);
                known = true;
            }
            flag({"schema", "project", "historicalProject", "lfs", "contextChanges", "diagnostics", "animationPose", "binaryLayouts", "virtualizedPayloads", "mesh-native"});
        } else if (kind == "property-layout") {
            columns("singleColumns", 3);
            columns("comparisonColumns", 5);
            if (key == "previewFraction") {
                scalar(x, .1, .7);
                known = true;
            }
        } else if (kind == "asset-layout") {
            columns("objectColumns", 2);
        } else if (kind == "graph-layout") {
            flag({"diagnostics"});
            columns("singleColumns", 3);
            columns("comparisonColumns", 5);
        } else if (kind == "container-layout") {
            columns("memberColumns", 4);
            columns("objectColumns", 2);
            flag({"diagnostics"});
        } else if (kind == "document") {
            child("layout", "shell-layout");
            child("asset", "asset");
            child("container", "container");
        } else if (kind == "property") {
            child("layout", "property-layout");
            string({"filter"});
            string({"selected"}, true);
            flag({"changedOnly"});
        } else if (kind == "list-filter") {
            string({"filter"});
            flag({"changedOnly"});
        } else if (kind == "graph-inspection") {
            string({"filter"});
            string({"selected"}, true);
            flag({"changedOnly"});
            child("properties", "property");
        } else if (kind == "canvas" || kind == "texture") {
            if (key == "pan") {
                vector(x, 2);
                known = true;
            }
            if (key == "zoom") {
                scalar(x, .000001, 1000000);
                known = true;
            }
            if (kind == "texture")
                flag({"difference"});
        } else if (kind == "graphs") {
            child("layout", "graph-layout");
            string({"selected"}, true);
            entries("views", "canvas");
            entries("inspectors", "graph-inspection");
            entries("parameterFilters", "list-filter");
        } else if (kind == "mesh") {
            flag({"changedOnly"});
            if (key == "target" || key == "up") {
                vector(x, 3);
                known = true;
                if (key == "up") {
                    double norm = 0;
                    for (const auto &a : x)
                        norm += a.get<double>() * a.get<double>();
                    require(norm > 1e-12 && norm < 1e12);
                }
            }
            if (key == "yaw") {
                scalar(x, -1e12, 1e12);
                known = true;
            }
            if (key == "pitch") {
                scalar(x, -1.56, 1.56);
                known = true;
            }
            if (key == "distance") {
                scalar(x, 1e-9, 1e12);
                known = true;
            }
        } else if (kind == "side") {
            entries("properties", "property");
            child("blueprint", "graphs");
            child("material", "graphs");
            entries("textures", "texture");
            child("mesh", "mesh");
        } else if (kind == "diff") {
            flag({"split"});
            string({"query"});
            if (key == "scroll") {
                scalar(x, 0, 1e9);
                known = true;
            }
        } else if(kind=="skeleton") {
            flag({"posePreview","animationPreview","attachmentsPreview"});
            child("attachments","attachments");
            child("animation","graph-inspection");
            string({"beforeRetargetSource","afterRetargetSource"});
            child("poseCamera","mesh");
            child("poseProperties","property");
            string({"object"},true);
            child("inspection","graph-inspection");
            child("canvas","canvas");
            child("metadata","property");
        } else if(kind=="curves"){
            string({"beforeObject","afterObject","lane","animationFamily"});
            if(key=="animationFamily")require(x=="float" || x=="transform");
            flag({"customRange"});
            if(key=="rangeBegin" || key=="rangeEnd"){scalar(x,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());known=true;}
            if(key=="valueTolerance"){scalar(x,0,std::numeric_limits<double>::max());known=true;}

            child("inspection","graph-inspection");
            if(key=="time"){scalar(x,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());known=true;}
        } else if(kind=="animation-pose"){
            string({"selected","filter"});
            child("camera","mesh");
            if(key=="positionTolerance"){scalar(x,0,std::numeric_limits<double>::max());known=true;}
        } else if(kind=="animation-source"){
            string({"beforeObject","afterObject"});
            child("inspection","graph-inspection");
            flag({"independentFrames"});
            if(key=="frame" || key=="afterFrame"){scalar(x,0,INT32_MAX);require(x.is_number_integer());known=true;}
            if(key=="positionTolerance" || key=="scaleTolerance"){scalar(x,0,std::numeric_limits<double>::max());known=true;}
            if(key=="rotationToleranceDegrees"){scalar(x,0,180);known=true;}
            if(key=="component"){scalar(x,0,9);require(x.is_number_integer());known=true;}
        } else if(kind=="attachments"){
            flag({"preview"});
            string({"beforeSource","afterSource"});
            child("inspection","graph-inspection");
            child("camera","mesh");
        } else if (kind == "collision") {
            child("viewport", "mesh");
            string({"body", "shape"}, true);
            child("properties", "property");
        } else if (kind == "asset") {
            child("meshMorphs", "property");
            flag({"meshMorphInspection"});
            child("meshSkin", "property");
            flag({"meshSkinInspection"});
            child("collisions", "collision");
            child("skeleton","skeleton");
            child("animations","animation-source");
            child("animationPose","animation-pose");
            child("animationSockets","animation-pose");
            flag({"animationPosePreview","animationSocketsPreview"});
            child("curves","curves");
            child("layout", "asset-layout");
            string({"filter"});
            string({"selected", "textureSelected"}, true);
            flag({"propertiesAfter", "rawProperties", "graphAfter", "materialAfter", "textureAfter",
                  "meshAfter"});
            child("before", "side");
            child("after", "side");
            child("comparison", "diff");
        } else if (kind == "container") {
            child("layout", "container-layout");
            string({"filter", "objectFilter", "pixel"});
            child("pixelView", "texture");
            child("meshView", "mesh");
            child("collisions", "collision");
            string({"mesh"});
            flag({"meshAfter"});
            flag({"pixelAfter"});
            string({"member", "object"}, true);
            flag({"packagesOnly", "changedOnly", "propertyMode"});
            for (const auto *field : {"inspection", "properties", "values", "comparison", "schema"})
                child(field, "property");
        }
        require(known);
    }
    if(kind=="skeleton")
        require(int(v.value("posePreview",false))+int(v.value("animationPreview",false))+int(v.value("attachmentsPreview",false))<=1);
    if(kind=="curves" && v.contains("rangeBegin") && v.contains("rangeEnd"))
        require(v["rangeBegin"].get<double>()<=v["rangeEnd"].get<double>());
    // Also validates programmatically supplied UTF-8, before any host resources are touched.
    require(v.dump().size() <= 256 * 1024);
}
} // namespace cyuediff::view_state
