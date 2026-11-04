#pragma once
#include <cyuediff/core.hpp>
#include <cyui/modules/Nodes.h>
namespace cyuediff {
// Serialized material graphs and instance overrides, prepared outside the render loop.
class MaterialView {
    struct Document {
        std::string path, parent;
        bool instance = false, complete = false, outputs_known = false, needs_fit = true;
        std::vector<std::string> diagnostics;
        Json parameters = Json::object();
        std::vector<cyui::Node> nodes;
        std::vector<cyui::NodeLink> links;
        cyui::NodeState state;
        cyui::Point low{}, high{};
        std::size_t pins = 0;
    };
    std::vector<Document> documents_;
    std::size_t selected_ = 0;

  public:
    void set_package(const Package &);
    bool empty() const {
        return documents_.empty();
    }
    bool select_object(const std::string &path);
    void draw(const char *id = "Material");
};
} // namespace cyuediff
