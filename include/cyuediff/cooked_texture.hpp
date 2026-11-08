#pragma once
#include <cyuediff/zen.hpp>
#include <cyuediff/gpu_texture.hpp>
#include <limits>
namespace cyuediff {
struct CookedTextureOptions {
    bool load_payloads = true;
    std::uint64_t max_payload_bytes = 256ull * 1024 * 1024;
    // Full verified package and external chunks, even when only a subrange is used.
    std::uint64_t max_read_bytes = 512ull * 1024 * 1024;
    std::uint32_t max_mips = 32, max_formats = 16, max_dimension = 65536;
    OperationControl control;
};
struct CookedTextureMip {
    std::uint32_t width = 0, height = 0, bulk_index = 0, bulk_flags = 0;
    std::uint64_t offset = 0, size = 0;
    std::string storage, chunk_id, chunk_source_hash, sha256, unavailable;
    bool loaded = false;
    std::vector<std::uint8_t> encoded;
    Json to_json() const;
};
struct CookedTexturePlatform {
    std::string format;
    std::uint32_t original_width = 0, original_height = 0, first_mip = 0;
    std::vector<CookedTextureMip> mips;
    Json to_json() const;
};
struct CookedTexture2D {
    std::string object_path, profile, package_sha256, schema_context_sha256, script_objects_sha256;
    std::uint64_t native_offset = 0, native_bytes = 0;
    std::size_t export_index = 0;
    std::vector<CookedTexturePlatform> platforms;
    Json to_json() const;
};
// Move-only worker session. One verified package parse and bounded external-chunk
// cache are reused across texture exports/mips. Use separate sessions on workers.
class CookedTextureReader {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit CookedTextureReader(std::unique_ptr<Impl>);

  public:
    ~CookedTextureReader();
    CookedTextureReader(CookedTextureReader &&) noexcept;
    CookedTextureReader &operator=(CookedTextureReader &&) noexcept;
    static CookedTextureReader open(const Container &, std::size_t package_entry, const ZenReadOptions &,
                                    const ContainerReadOptions & = {}, const CookedTextureOptions & = {});
    const ZenPackage &package() const;
    CookedTexture2D read(std::size_t export_index = std::numeric_limits<std::size_t>::max());
};
// Exact UE 5.3 editor-stripped Texture2D BulkData profile. Requires successfully
// decoded properties from an explicit matching context. No native-header scanning.
// A default export index selects the sole non-template Texture2D in the package.
// Every external chunk is read whole through Container::read and verified before use.
// Missing optional/bulk chunks retain unavailable mip records; integrity errors propagate.
CookedTexture2D read_zen_texture2d(const Container &, std::size_t package_entry, const ZenReadOptions &,
                                   const ContainerReadOptions & = {}, const CookedTextureOptions & = {},
                                   std::size_t export_index = std::numeric_limits<std::size_t>::max());
ImageRgba8 decode_cooked_mip(const CookedTexturePlatform &, std::size_t mip = 0,
                             const GpuDecodeOptions & = {});
ImageRgba32f decode_cooked_mip_hdr(const CookedTexturePlatform &, std::size_t mip = 0,
                                   const GpuDecodeOptions & = {});
} // namespace cyuediff
