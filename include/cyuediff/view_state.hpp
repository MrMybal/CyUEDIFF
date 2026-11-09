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
    if (kind == "cooked-curves" && v.contains("rangeStart") && v.contains("rangeEnd")) {
        require(v.at("rangeStart").is_number() && v.at("rangeEnd").is_number());
        require(v.at("rangeStart").get<double>() < v.at("rangeEnd").get<double>());
    }
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
            flag({"schema", "project", "historicalProject", "lfs", "contextChanges", "diagnostics",
                  "animationPose", "binaryLayouts", "virtualizedPayloads", "mesh-native", "physics-native",
                  "animation-native", "skeletal-native", "nanite-native", "physical-cloth-native"});
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
        } else if (kind == "nanite-layout") {
            columns("columns", 2);
        } else if (kind == "container-layout") {
            columns("memberColumns", 4);
            columns("objectColumns", 2);
            flag({"diagnostics"});
        } else if (kind == "document") {
            child("layout", "shell-layout");
            child("asset", "asset");
            child("container", "container");
        } else if (kind == "property-browser") {
            string({"scope"});
            if (key == "scope") {
                require(x.get_ref<const std::string &>().size() <= 4096);
                const auto &scope = x.get_ref<const std::string &>();
                std::size_t depth = 0;
                for (char c : scope)
                    if (c == '/')
                        ++depth;
                require(depth <= 128);
                try {
                    (void)Json::json_pointer(scope);
                } catch (const Json::exception &) {
                    require(false);
                }
            }
            flag({"serializationFields"});
            child("properties", "property");
            if (key == "first") {
                require(x.is_number_integer());
                scalar(x, 0, 2'000'000);
                known = true;
            }
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
        } else if (kind == "skeleton") {
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
        } else if (kind == "curves") {
            string({"beforeObject","afterObject","lane","animationFamily"});
            if(key=="animationFamily")require(x=="float" || x=="transform");
            flag({"customRange"});
            if(key=="rangeBegin" || key=="rangeEnd"){scalar(x,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());known=true;}
            if(key=="valueTolerance"){scalar(x,0,std::numeric_limits<double>::max());known=true;}

            child("inspection","graph-inspection");
            if(key=="time"){scalar(x,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());known=true;}
        } else if (kind == "animation-pose") {
            string({"selected","filter"});
            child("camera","mesh");
            if(key=="positionTolerance"){scalar(x,0,std::numeric_limits<double>::max());known=true;}
        } else if (kind == "animation-source") {
            string({"beforeObject","afterObject"});
            child("inspection","graph-inspection");
            flag({"independentFrames"});
            if(key=="frame" || key=="afterFrame"){scalar(x,0,INT32_MAX);require(x.is_number_integer());known=true;}
            if(key=="positionTolerance" || key=="scaleTolerance"){scalar(x,0,std::numeric_limits<double>::max());known=true;}
            if(key=="rotationToleranceDegrees"){scalar(x,0,180);known=true;}
            if(key=="component"){scalar(x,0,9);require(x.is_number_integer());known=true;}
        } else if (kind == "attachments") {
            flag({"preview"});
            string({"beforeSource","afterSource"});
            child("inspection","graph-inspection");
            child("camera","mesh");
        } else if (kind == "collision") {
            child("viewport", "mesh");
            string({"body", "shape"}, true);
            child("properties", "property");
        } else if (kind == "cooked-curves") {
            string({"selected", "filter"});
            flag({"independentTimes", "changedOnly"});
            child("properties", "property");
            if (key == "time" || key == "afterTime" || key == "emptyDefault" || key == "rangeStart" ||
                key == "rangeEnd") {
                scalar(x, -double(std::numeric_limits<float>::max()),
                       double(std::numeric_limits<float>::max()));
                known = true;
            }
            if (key == "valueTolerance") {
                scalar(x, 0, std::numeric_limits<double>::max());
                known = true;
            }
            if (key == "beforeKey" || key == "afterKey") {
                require(x.is_number_integer());
                scalar(x, 0, 2147483647.);
                known = true;
            }
        } else if (kind == "cooked-animation") {
            flag({"scalarMode"});
            child("curves", "cooked-curves");
            string({"selected", "filter"});
            flag({"independentFrames", "changedOnly"});
            child("properties", "property");
            if (key == "frame" || key == "afterFrame") {
                scalar(x, 0, 2147483646.);
                known = true;
            }
            if (key == "component") {
                require(x.is_number_integer());
                scalar(x, 0, 9);
                known = true;
            }
            if (key == "beforeKey" || key == "afterKey") {
                require(x.is_number_integer());
                scalar(x, 0, 2147483647.);
                known = true;
            }
            if (key == "positionTolerance" || key == "scaleTolerance") {
                scalar(x, 0, std::numeric_limits<double>::max());
                known = true;
            }
            if (key == "rotationToleranceDegrees") {
                scalar(x, 0, 180);
                known = true;
            }
        } else if (kind == "nanite") {
            string({"filter"});
            flag({"after", "clusterPreview", "attributes", "changedOnly", "compactSurface"});
            child("layout", "nanite-layout");
            child("properties", "property");
            child("camera", "mesh");
            for (const auto *name : {"page", "cluster", "vertex", "triangle", "node", "inspector"})
                if (key == name) {
                    require(x.is_number_integer());
                    scalar(x, 0,
                           key == "page"                         ? 16383
                           : key == "cluster" || key == "vertex" ? 255
                           : key == "triangle"                   ? 127
                           : key == "node"                       ? 65535
                                                                 : 3);
                    known = true;
                }
        } else if (kind == "vertex-attribute") {
            string({"channel", "filter"});
            flag({"changedOnly"});
            child("properties", "property");
            if (key == "vertex") {
                if (!x.is_null()) {
                    require(x.is_number_integer());
                    scalar(x, 0, 4294967295.);
                }
                known = true;
            }
        } else if (kind == "physical-cloth") {
            string({"filter"});
            flag({"changedOnly", "after", "controlsOpen"});
            child("details", "property");
            child("mesh", "mesh");
            if (key == "beforeLOD" || key == "afterLOD" || key == "selected") {
                if (!x.is_null()) {
                    require(x.is_number_integer());
                    scalar(x, 0, 4294967295.);
                }
                known = true;
            }
            if (key == "mode" || key == "panel") {
                require(x.is_number_integer());
                scalar(x, 0, 2);
                known = true;
            }
        } else if (kind == "cloth") {
            string({"filter"});
            flag({"changedOnly"});
            child("properties", "property");
            if (key == "section" || key == "lodBias" || key == "vertex") {
                if (key != "vertex" || !x.is_null()) {
                    require(x.is_number_integer());
                    scalar(x, 0, 4294967295.);
                }
                known = true;
            }
        } else if (kind == "render-morph") {
            string({"target", "filter"});
            flag({"changedOnly"});
            child("properties", "property");
            if (key == "vertex") {
                if (!x.is_null()) {
                    require(x.is_number_integer());
                    scalar(x, 0, 4294967295.);
                }
                known = true;
            }
        } else if (kind == "asset") {
            child("propertyBrowser", "property-browser");
            flag({"browseProperties"});
            string({"browserObject"});
            if (key == "browserRoot") {
                require(x.is_number_integer());
                scalar(x, 0, 2);
                known = true;
            }
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
            child("physicalCloth", "physical-cloth");
            string({"beforePhysicalCloth", "afterPhysicalCloth"}, true);
            flag({"physicalPairExplicit"});
            child("propertyBrowser", "property-browser");
            flag({"browseProperties"});
            child("nanite", "nanite");
            string({"beforeNanite", "afterNanite"}, true);
            child("animation", "cooked-animation");
            string({"beforeAnimation", "afterAnimation"}, true);
            child("layout", "container-layout");
            string({"filter", "objectFilter", "pixel"});
            child("pixelView", "texture");
            child("meshView", "mesh");
            child("meshSkinView", "graph-inspection");
            child("meshAttributeView", "vertex-attribute");
            flag({"meshAttributes"});
            child("meshClothView", "cloth");
            flag({"meshCloth"});
            child("meshRenderMorphView", "render-morph");
            flag({"meshRenderMorphs"});
            flag({"meshSkin"});
            child("collisions", "collision");
            child("physics", "collision");
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
    if (kind == "container")
        require(int(v.value("meshSkin", false)) + int(v.value("meshRenderMorphs", false)) +
                    int(v.value("meshCloth", false)) + int(v.value("meshAttributes", false)) <=
                1);
    if(kind=="skeleton")
        require(int(v.value("posePreview",false))+int(v.value("animationPreview",false))+int(v.value("attachmentsPreview",false))<=1);
    if(kind=="curves" && v.contains("rangeBegin") && v.contains("rangeEnd"))
        require(v["rangeBegin"].get<double>()<=v["rangeEnd"].get<double>());
    // Also validates programmatically supplied UTF-8, before any host resources are touched.
    require(v.dump().size() <= 256 * 1024);
}
} // namespace cyuediff::view_state
