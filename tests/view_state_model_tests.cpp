#include <cyuediff/view_state.hpp>
#include <iostream>
#include <limits>
using namespace cyuediff;
int main() {
    int checks = 0;
    try {
        auto rejects = [&](const Json &j, const char *kind) {
            bool failed = false;
            try {
                view_state::validate(j, kind);
            } catch (const std::exception &) {
                failed = true;
            }
            if (!failed)
                throw std::runtime_error("Expected rejected view state");
            ++checks;
        };
        for (const auto *kind : {"document", "asset", "side", "property", "graphs", "canvas", "texture",
                                 "mesh", "diff", "container"}) {
            view_state::validate(Json::object(), kind);
            ++checks;
        }
        for (const auto *kind :
             {"shell-layout", "property-layout", "asset-layout", "graph-layout", "container-layout"}) {
            view_state::validate(Json::object(), kind);
            ++checks;
        }
        view_state::validate(
            {{"layout", {{"sidebarWidth", 400}, {"diagnostics", true}}},
             {"asset",
              {{"layout", {{"objectColumns", {1, 2}}}},
               {"before",
                {{"properties",
                  {{"A", {{"layout", {{"previewFraction", .4}, {"singleColumns", {2, 1, 3}}}}}}}}}}}}});
        ++checks;
        for (const auto &bad : {Json{{"sidebarWidth", 0}}, Json{{"sidebarWidth", 1e9}},
                                Json{{"diagnostics", 1}}, Json{{"unknown", false}}})
            rejects(bad, "shell-layout");
        rejects({{"layout", {{"singleColumns", {1, 2}}}}}, "property");
        rejects({{"layout", {{"comparisonColumns", {1, 2, 3, 4, 0}}}}}, "property");
        rejects({{"layout", {{"previewFraction", .99}}}}, "property");
        rejects({{"layout", {{"objectColumns", {1, std::numeric_limits<double>::infinity()}}}}}, "asset");
        rejects({{"layout", {{"memberColumns", {1, 2, 3, "4"}}}}}, "container");
        Json property = {
            {"filter", "Health"}, {"changedOnly", true}, {"selected", "/properties/Health/value"}};
        Json canvas = {{"pan", {-35.5, 420.0}}, {"zoom", .75}};
        Json graph = {{"selected", "GraphGuid"}, {"views", {{"GraphGuid", canvas}}}};
        Json camera = {
            {"target", {1, 2, 3}}, {"up", {0, 0, 1}}, {"yaw", .7}, {"pitch", .35}, {"distance", 55}};
        Json asset = {
            {"selected", "/Game/Player.TestAsset"},
            {"before", {{"properties", {{"TestAsset", property}}}, {"blueprint", graph}, {"mesh", camera}}},
            {"comparison", {{"split", false}, {"query", "Health"}, {"scroll", 128.0}}}};
        Json document = {
            {"asset", asset},
            {"container",
             {{"filter", "Player"}, {"object", nullptr}, {"member", "0001"}, {"values", property}}}};
        document["container"]["meshSkin"]=true;document["container"]["meshSkinView"]={{"selected","vertex/39999"},{"filter","Root"},{"changedOnly",false},{"properties",property}};
        view_state::validate(document);
        ++checks;
        view_state::validate({{"inspectors",
                               {{"Graph",
                                 {{"filter", "Power"},
                                  {"changedOnly", true},
                                  {"selected", "node"},
                                  {"properties", property}}}}},
                              {"parameterFilters", {{"MI", {{"filter", "Power"}, {"changedOnly", true}}}}}},
                             "graphs");
        ++checks;
        rejects({{"inspectors", {{"Graph", {{"selected", 1}}}}}}, "graphs");
        rejects({{"inspectors", {{"Graph", {{"properties", {{"changedOnly", 1}}}}}}}}, "graphs");
        rejects({{"parameterFilters", {{"MI", {{"filter", 1}}}}}}, "graphs");
        rejects({{"changedOnly", 1}}, "mesh");
        rejects({{"meshSkin",1}},"container");rejects({{"meshSkinView",{{"selected",17}}}},"container");
        rejects({{"codec", "x.dll"}}, "document");
        rejects({{"selected", 5}}, "asset");
        rejects({{"changedOnly", 1}}, "property");
        rejects({{"filter", std::string(4097, 'a')}}, "property");
        rejects({{"filter", std::string("a\0b", 3)}}, "property");
        rejects({{"filter", std::string(1, char(0xff))}}, "property");
        for (double v : {0., -1., 1e9, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()})
            rejects({{"zoom", v}}, "canvas");
        rejects({{"pan", {1}}}, "canvas");
        rejects({{"pan", {1, 2, 3}}}, "canvas");
        rejects({{"pan", {1, 1e20}}}, "canvas");
        rejects({{"distance", 0}}, "mesh");
        rejects({{"pitch", 1.6}}, "mesh");
        rejects({{"up", {0, 0, 0}}}, "mesh");
        rejects({{"scroll", -1}}, "diff");
        rejects({{"query", false}}, "diff");
        rejects({{"selected", 1}}, "graphs");
        Json many = Json::object();
        for (int i = 0; i < 129; ++i)
            many[std::to_string(i)] = canvas;
        rejects({{"views", many}}, "graphs");
        Json large = Json::object();
        for (int i = 0; i < 128; ++i)
            large[std::string(2100, 'x') + std::to_string(i)] = property;
        rejects({{"properties", large}}, "side");
        view_state::validate({{"selected", nullptr}}, "property");
        ++checks;
        std::cout << checks << " view state model checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
