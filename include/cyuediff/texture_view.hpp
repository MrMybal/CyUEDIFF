#pragma once
#include <cyuediff/core.hpp>
#include <cyui/modules/Images.h>
namespace cyuediff {
// The host decodes the image off-thread, uploads RGBA8 and owns the GPU resource.
// Clear the binding before destroying that resource. draw() performs no file I/O.
class TextureView {
    TextureSource source_;
    cyui::ImageData image_;
    cyui::ImageState state_;

  public:
    void set_source(TextureSource source);
    bool set_preview(const cyui::ImageData &image);
    void clear_preview();
    const std::string &object_path() const {
        return source_.object_path;
    }
    void draw(const char *id = "CyUEDiffTexture");
};
} // namespace cyuediff
