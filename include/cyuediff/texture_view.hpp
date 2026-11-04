#pragma once
#include <cyuediff/view_state.hpp>
#include <cyuediff/hdr.hpp>
#include <cyui/modules/Images.h>
#include <optional>
namespace cyuediff {
// The host decodes the image off-thread, uploads RGBA8 and owns the GPU resource.
// Clear the binding before destroying that resource. draw() performs no file I/O.
class TextureView {
    TextureSource source_;
    cyui::ImageData image_, difference_;
    bool show_difference_ = false, focus_changes_pending_ = false;
    std::optional<ImageComparison> comparison_;
    std::optional<double> hdr_delta_;
    cyui::ImageState state_;

  public:
    // Owner-thread snapshot/restore after binding the current data and previews; no file I/O.
    Json view_state() const;
    void restore_view_state(const Json &);
    void set_source(TextureSource source);
    bool set_preview(const cyui::ImageData &image);
    void clear_preview();
    bool focus_changes();
    bool set_difference(const cyui::ImageData &, const ImageComparison &);
    bool set_difference(const cyui::ImageData &, const HdrImageComparison &);
    const std::string &object_path() const {
        return source_.object_path;
    }
    void draw(const char *id = "CyUEDiffTexture");
};
} // namespace cyuediff
