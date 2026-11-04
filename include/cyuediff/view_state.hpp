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
            flag({"schema", "project", "historicalProject", "lfs", "contextChanges", "diagnostics"});
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
        } else if (kind == "mesh") {
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
        } else if (kind == "asset") {
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
            string({"filter", "objectFilter"});
            string({"member", "object"}, true);
            flag({"packagesOnly", "changedOnly", "propertyMode"});
            for (const auto *field : {"inspection", "properties", "values", "comparison", "schema"})
                child(field, "property");
        }
        require(known);
    }
    // Also validates programmatically supplied UTF-8, before any host resources are touched.
    require(v.dump().size() <= 256 * 1024);
}
} // namespace cyuediff::view_state
