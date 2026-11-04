#pragma once
#include "native.hpp"
namespace cyuediff::detail {
struct EditorBulkResult { BulkPayload payload;std::uint32_t flags=0;std::string guid; };
EditorBulkResult read_editor_bulk(Archive&,const Package&,const ReadOptions&);
struct TextureDecodeResult {
    TextureSource source;
    Json native;
    std::uint64_t end = 0;
};
TextureSource texture_metadata(const ObjectExport &object);
bool is_texture2d(const ObjectExport &);
TextureDecodeResult decode_texture_source(Archive &, const Package &, std::size_t, std::uint64_t,
                                          const ReadOptions &);
// Exposed internally for focused binary fixtures; no scan for payload signatures.
TexturePayload locate_texture_payload(const fs::path &, const std::string &content_id, std::uint64_t raw_size,
                                      const ReadOptions &);
} // namespace cyuediff::detail
