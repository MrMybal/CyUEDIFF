#include <cyuediff/texture_view.hpp>
namespace cyuediff {
void TextureView::set_source(TextureSource source) {
    source_ = std::move(source);
    clear_preview();
}
bool TextureView::set_preview(const cyui::ImageData &image) {
    if (!image.texture || image.width <= 0 || image.height <= 0)
        return false;
    image_ = image;
    state_ = {};
    return true;
}
bool TextureView::set_difference(const cyui::ImageData &image, const ImageComparison &comparison) {
    if (comparison.comparable &&
        (!image.texture || image.width != int(source_.width) || image.height != int(source_.height)))
        return false;
    // Retain scalar statistics without copying a potentially large heatmap.
    comparison_ = ImageComparison{comparison.comparable,
                                  comparison.same_size,
                                  comparison.before_width,
                                  comparison.before_height,
                                  comparison.after_width,
                                  comparison.after_height,
                                  comparison.changed_pixels,
                                  comparison.max_channel_delta,
                                  comparison.mean_absolute_error,
                                  comparison.threshold,
                                  {}};
    difference_ = image;
    show_difference_ = comparison.comparable;
    return true;
}
void TextureView::clear_preview() {
    difference_ = {};
    comparison_.reset();
    show_difference_ = false;
    image_ = {};
    state_ = {};
}
void TextureView::draw(const char *id) {
    cyui::ScopedID scope(id);
    if (source_.object_path.empty()) {
        cyui::emptyState(cyui::tr("No texture source").c_str(),
                         cyui::tr("This package has no decoded Texture2D source.").c_str(),
                         cyui::icon::document);
        return;
    }
    cyui::panelHeader(source_.object_path.c_str());
    cyui::textDim("%u x %u | %s | %s", source_.width, source_.height, source_.format.c_str(),
                  source_.compression.c_str());
    for (const auto &diagnostic : source_.diagnostics)
        cyui::callout(cyui::Tone::warning, diagnostic.c_str());
    if (!image_.texture) {
        cyui::emptyState(cyui::tr("Preview unavailable").c_str(),
                         cyui::tr("Load a supported source preview in the application.").c_str(),
                         cyui::icon::document);
        return;
    }
    if (comparison_) {
        if (comparison_->comparable) {
            ImGui::Checkbox(cyui::trLabel("Pixel difference mask").c_str(), &show_difference_);
            ImGui::SameLine();
            cyui::textDim(cyui::tr("%llu changed pixels | maximum delta: %u").c_str(),
                          static_cast<unsigned long long>(comparison_->changed_pixels),
                          comparison_->max_channel_delta);
        } else
            cyui::callout(cyui::Tone::warning, cyui::tr("Images have different dimensions.").c_str());
    }
    if (ImGui::Button(cyui::trLabel("Fit image").c_str()))
        state_.fitPending = true;
    auto result = cyui::imageViewport("##image", state_, show_difference_ ? difference_ : image_,
                                      ImGui::GetContentRegionAvail());
    if (result.pixelX >= 0)
        ImGui::SetTooltip(cyui::tr("Pixel: %d, %d").c_str(), result.pixelX, result.pixelY);
}
} // namespace cyuediff
