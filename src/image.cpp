#include "image_bounds.hpp"
#include "archive.hpp"
#include <cyuediff/hdr.hpp>
#include <climits>
#include <cmath>
#include <fstream>
#include <memory>
#include <cstddef>
#include <cstdlib>
namespace {
struct ImageBudget {
    std::uint64_t limit = 0, used = 0;
    bool exceeded = false;
};
thread_local ImageBudget *active_budget = nullptr;
struct alignas(std::max_align_t) AllocationHeader {
    std::size_t bytes;
};
void *image_malloc(std::size_t bytes) {
    if (bytes > SIZE_MAX - sizeof(AllocationHeader))
        return nullptr;
    if (active_budget && bytes > active_budget->limit - active_budget->used) {
        active_budget->exceeded = true;
        return nullptr;
    }
    auto memory = static_cast<AllocationHeader *>(std::malloc(sizeof(AllocationHeader) + bytes));
    if (!memory)
        return nullptr;
    memory->bytes = bytes;
    if (active_budget)
        active_budget->used += bytes;
    return memory + 1;
}
void image_free(void *data) {
    if (!data)
        return;
    auto memory = static_cast<AllocationHeader *>(data) - 1;
    if (active_budget)
        active_budget->used -= memory->bytes;
    std::free(memory);
}
void *image_realloc(void *data, std::size_t bytes) {
    if (!data)
        return image_malloc(bytes);
    if (bytes > SIZE_MAX - sizeof(AllocationHeader))
        return nullptr;
    auto memory = static_cast<AllocationHeader *>(data) - 1;
    auto old = memory->bytes;
    if (active_budget && bytes > active_budget->limit - (active_budget->used - old)) {
        active_budget->exceeded = true;
        return nullptr;
    }
    auto resized = static_cast<AllocationHeader *>(std::realloc(memory, sizeof(AllocationHeader) + bytes));
    if (!resized)
        return nullptr;
    resized->bytes = bytes;
    if (active_budget)
        active_budget->used = active_budget->used - old + bytes;
    return resized + 1;
}
class ImageBudgetScope {
    ImageBudget *previous_;

  public:
    ImageBudget budget;
    explicit ImageBudgetScope(std::uint64_t limit) : previous_(active_budget) {
        budget.limit = limit;
        active_budget = &budget;
    }
    ~ImageBudgetScope() {
        active_budget = previous_;
    }
};
} // namespace
#define STBI_MALLOC(bytes) image_malloc(bytes)
#define STBI_REALLOC(data, bytes) image_realloc(data, bytes)
#define STBI_FREE(data) image_free(data)
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
namespace cyuediff {
namespace {
std::uint64_t pixels(const ImageRgba8 &image) {
    auto count = std::uint64_t(image.width) * image.height;
    if (!count || count > SIZE_MAX / 4 || count * 4 != image.pixels.size())
        throw Error("image-layout", "Invalid RGBA8 image buffer");
    return count;
}
unsigned bpp(const std::string &format) {
    if (format == "TSF_G8")
        return 1;
    if (format == "TSF_G16")
        return 2;
    if (format == "TSF_BGRA8" || format == "TSF_RGBA8" || format == "TSF_RGBA8_DEPRECATED")
        return 4;
    if (format == "TSF_RGBA16")
        return 8;
    throw Error("texture-format", "No RGBA8 preview conversion for " + format);
}
void dimension_limit(std::uint32_t width, std::uint32_t height, const TextureDecodeOptions &options) {
    auto count = std::uint64_t(width) * height;
    if (!count || count > options.max_pixels || count > SIZE_MAX / 4 || width > INT_MAX / 4 ||
        height > INT_MAX)
        throw Error("limit", "Texture dimensions exceed preview limits");
}
} // namespace
ImageRgba8 decode_texture(const TextureSource &texture, std::uint32_t mip,
                          const TextureDecodeOptions &options) {
    options.control.checkpoint("Decoding image");
    if (is_hdr_format(texture.format))
        return tone_map_hdr(decode_texture_hdr(texture, mip, options), 0, options.control);
    if (!texture.mips || mip >= texture.mips || texture.mips > 32)
        throw Error("texture-mip", "Requested source mip does not exist");
    if (texture.slices != 1 || texture.layers != 1 || texture.block_offsets.size() != 1 ||
        texture.block_offsets[0] != 0)
        throw Error("texture-layout",
                    "Preview currently supports one 2D source block, one layer and one slice");
    ImageRgba8 image;
    image.width = std::max(1u, texture.width >> mip);
    image.height = std::max(1u, texture.height >> mip);
    if (!texture.width || !texture.height)
        throw Error("texture-metadata", "Texture has no source dimensions");
    dimension_limit(image.width, image.height, options);
    auto source = decode_bulk_payload(texture.payload, options);
    if (texture.compression == "TSCF_PNG" || texture.compression == "TSCF_JPEG") {
        if (texture.mips != 1 || mip)
            throw Error("texture-mip", "Encoded source preview requires a single source mip");
        if (source.size() > INT_MAX)
            throw Error("limit", "Encoded image exceeds decoder input limit");
        const std::vector<std::uint8_t> png_magic = {137, 80, 78, 71, 13, 10, 26, 10};
        if ((texture.compression == "TSCF_PNG" &&
             (source.size() < 8 || !std::equal(png_magic.begin(), png_magic.end(), source.begin()))) ||
            (texture.compression == "TSCF_JPEG" &&
             (source.size() < 2 || source[0] != 255 || source[1] != 216)))
            throw Error("texture-image", "Source signature disagrees with declared encoding");
        ImageBudgetScope budget(options.max_decoder_bytes);
        int width = 0, height = 0, channels = 0;
        if (!stbi_info_from_memory(source.data(), static_cast<int>(source.size()), &width, &height,
                                   &channels)) {
            if (budget.budget.exceeded)
                throw Error("limit", "Image decoder exceeded its allocation budget");
            throw Error("texture-image", "Invalid PNG/JPEG source image");
        }
        if (width <= 0 || height <= 0 || static_cast<std::uint32_t>(width) != image.width ||
            static_cast<std::uint32_t>(height) != image.height)
            throw Error("texture-dimensions", "Encoded source dimensions disagree with the texture metadata");
        dimension_limit(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), options);
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
            stbi_load_from_memory(source.data(), static_cast<int>(source.size()), &width, &height, &channels,
                                  4),
            stbi_image_free);
        if (!decoded) {
            if (budget.budget.exceeded)
                throw Error("limit", "Image decoder exceeded its allocation budget");
            throw Error("texture-image", "Unable to decode PNG/JPEG source image");
        }
        options.control.checkpoint("Decoding image", 1, 1);
        image.pixels.assign(decoded.get(), decoded.get() + std::size_t(width) * height * 4);
        return image;
    }
    if (texture.compression != "TSCF_None")
        throw Error("texture-codec", "Unsupported texture source encoding: " + texture.compression);
    auto stride = bpp(texture.format);
    std::uint64_t offset = 0, total = 0;
    for (std::uint32_t level = 0; level < texture.mips; ++level) {
        auto count =
            std::uint64_t(std::max(1u, texture.width >> level)) * std::max(1u, texture.height >> level);
        if (count > options.max_payload_bytes / stride)
            throw Error("limit", "Raw mip exceeds payload limit");
        auto size = count * stride;
        if (size > options.max_payload_bytes - total)
            throw Error("limit", "Raw mip chain exceeds payload limit");
        if (level < mip)
            offset += size;
        total += size;
    }
    if (total != source.size())
        throw Error("texture-layout", "Raw source size does not match its mip chain");
    auto count = std::uint64_t(image.width) * image.height;
    image.pixels.resize(static_cast<std::size_t>(count * 4));
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 65536 == 0)
            options.control.checkpoint("Converting image", i, count);
        auto input = source.data() + offset + i * stride;
        auto output = image.pixels.data() + i * 4;
        output[3] = 255;
        if (texture.format == "TSF_G8" || texture.format == "TSF_G16")
            output[0] = output[1] = output[2] = input[stride - 1];
        else if (texture.format == "TSF_BGRA8") {
            output[0] = input[2];
            output[1] = input[1];
            output[2] = input[0];
            output[3] = input[3];
        } else if (stride == 8)
            for (unsigned c = 0; c < 4; ++c)
                output[c] = input[c * 2 + 1];
        else
            std::copy(input, input + 4, output);
    }
    return image;
}
ImageComparison compare_images(const ImageRgba8 &before, const ImageRgba8 &after, std::uint8_t threshold,
                               bool heatmap) {
    auto before_count = pixels(before);
    pixels(after);
    ImageComparison result;
    result.threshold = threshold;
    result.before_width = before.width;
    result.before_height = before.height;
    result.after_width = after.width;
    result.after_height = after.height;
    result.same_size = before.width == after.width && before.height == after.height;
    result.comparable = result.same_size;
    if (!result.same_size) {
        if (heatmap)
            throw Error("image-size", "Difference mask requires matching image dimensions");
        return result;
    }
    if (heatmap) {
        result.heatmap.width = before.width;
        result.heatmap.height = before.height;
        result.heatmap.pixels.assign(before.pixels.size(), 0);
    }
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < before_count; ++i) {
        unsigned maximum = 0;
        for (unsigned c = 0; c < 4; ++c) {
            auto delta =
                static_cast<unsigned>(std::abs(int(before.pixels[i * 4 + c]) - int(after.pixels[i * 4 + c])));
            sum += delta;
            maximum = std::max(maximum, delta);
        }
        result.max_channel_delta = std::max(result.max_channel_delta, maximum);
        if (maximum > threshold) {
            ++result.changed_pixels;
            detail::include_changed_pixel(result.changed_bounds, static_cast<std::uint32_t>(i % before.width),
                                          static_cast<std::uint32_t>(i / before.width));
            if (heatmap) {
                result.heatmap.pixels[i * 4] = 255;
                result.heatmap.pixels[i * 4 + 3] = 255;
            }
        }
    }
    result.mean_absolute_error = static_cast<double>(sum) / (static_cast<double>(before_count) * 4.0);
    return result;
}
Json ImageChangeBounds::to_json() const {
    return width && height ? Json{{"x", x}, {"y", y}, {"width", width}, {"height", height}} : Json(nullptr);
}
Json ImageComparison::to_json() const {
    return {{"schema", "cyuediff.image-diff/1"},
            {"status", comparable && changed_pixels == 0 ? "identical" : "changed"},
            {"comparison", "RGBA8 encoded samples; no color-space conversion"},
            {"sameSize", same_size},
            {"pixelsComparable", comparable},
            {"before", {{"width", before_width}, {"height", before_height}}},
            {"after", {{"width", after_width}, {"height", after_height}}},
            {"threshold", threshold},
            {"changedPixels", comparable ? Json(changed_pixels) : Json(nullptr)},
            {"changedBounds", comparable && changed_pixels ? changed_bounds.to_json() : Json(nullptr)},
            {"maxChannelDelta", comparable ? Json(max_channel_delta) : Json(nullptr)},
            {"meanAbsoluteError", comparable ? Json(mean_absolute_error) : Json(nullptr)}};
}
void write_png(const fs::path &path, const ImageRgba8 &image) {
    auto count = pixels(image);
    if (image.width > INT_MAX / 4 || image.height > INT_MAX ||
        count > (std::uint64_t(INT_MAX) - image.height) / 4)
        throw Error("limit", "PNG dimensions exceed encoder limits");
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        throw Error("io", "Cannot create preview image");
    auto callback = [](void *context, void *bytes, int count) {
        auto &target = *static_cast<std::ofstream *>(context);
        target.write(static_cast<const char *>(bytes), count);
    };
    if (!stbi_write_png_to_func(callback, &stream, static_cast<int>(image.width),
                                static_cast<int>(image.height), 4, image.pixels.data(),
                                static_cast<int>(image.width * 4)) ||
        !stream)
        throw Error("io", "Cannot write preview image");
}
} // namespace cyuediff
