#pragma once
#include <cyuediff/zen.hpp>
#include "archive.hpp"
namespace cyuediff::detail {
// Zen-map byte payloads. Range validation precedes loading; every used external
// chunk is read in full through Container's mandatory source-integrity check.
struct ZenBulkWindow {
    Json metadata;
    const std::vector<std::uint8_t> *data = nullptr;
    std::uint64_t offset = 0, bytes = 0;
    std::size_t chunk = SIZE_MAX;
    std::string unavailable;
};
class ZenBulkReader {
    const Container &container_;
    const ZenPackage &package_;
    const ContainerEntry &member_;
    const std::vector<std::uint8_t> &bytes_;
    ContainerReadOptions reads_;
    std::uint64_t total_, maximum_;
    std::string prefix_;
    std::map<std::string, std::vector<std::uint8_t>> chunks_;
    void need(bool b, const char *s) const {
        if (!b)
            throw Error(prefix_ + "-layout", s);
    }
    std::size_t find(const std::string &id) const {
        std::size_t found = SIZE_MAX;
        for (std::size_t i = 0; i < container_.entries().size(); ++i) {
            if ((i & 4095) == 0)
                reads_.control.check();
            const auto &e = container_.entries()[i];
            if (e.id == id && !e.deleted) {
                need(found == SIZE_MAX, "Ambiguous Zen bulk chunk");
                found = i;
            }
        }
        return found;
    }

  public:
    ZenBulkReader(const Container &c, const ZenPackage &p, const ContainerEntry &m,
                  const std::vector<std::uint8_t> &b, ContainerReadOptions reads, std::uint64_t maximum,
                  std::string prefix)
        : container_(c), package_(p), member_(m), bytes_(b), reads_(std::move(reads)), total_(b.size()),
          maximum_(maximum), prefix_(std::move(prefix)) {
        if (total_ > maximum_)
            throw Error(prefix_ + "-limit", "Package exceeds aggregate Zen bulk read budget");
    }
    ZenBulkWindow describe(std::uint32_t index, std::uint64_t &inline_pos, std::uint64_t inline_end) const {
        reads_.control.check();
        need(index < package_.bulk_data.size(), "Zen bulk-map index exceeds table");
        const auto &row = package_.bulk_data[index];
        const auto flags = row.at("flags").get<std::uint32_t>();
        if (flags & ~0x1ffffu || (flags & ((1u << 1) | (1u << 9) | (1u << 15))))
            throw Error(prefix_ + "-profile", "Unsupported Zen bulk flags or legacy compression");
        ZenBulkWindow out;
        out.bytes = row.at("size").get<std::uint64_t>();
        auto id = member_.id;
        const bool unused = flags & (1u << 5), external = flags & 1;
        bool offset_unknown = false;
        if (unused)
            out.unavailable = "unused";
        else if (!external) {
            need(!(flags & ((1u << 8) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 14))),
                 "Conflicting inline Zen bulk flags");
            out.offset = inline_pos;
            need(inline_end <= bytes_.size() && inline_pos <= inline_end &&
                     out.bytes <= inline_end - inline_pos,
                 "Inline Zen bulk payload exceeds export window");
            inline_pos += out.bytes;
            out.data = &bytes_;
        } else {
            auto off = row.at("offset").get<std::int64_t>();
            need(off >= 0 || (!out.bytes && off == -1), "Negative external Zen bulk offset");
            offset_unknown = off < 0;
            out.offset = off < 0 ? 0 : static_cast<std::uint64_t>(off);
            unsigned type = flags & (1u << 11) ? 3 : 2;
            if (flags & (1u << 14)) {
                auto optional = member_.id;
                optional.replace(22, 2, "03");
                if (find(optional) != SIZE_MAX) {
                    type = 3;
                    off = row.at("duplicateOffset").get<std::int64_t>();
                    need(off >= 0 || (!out.bytes && off == -1), "Negative duplicate Zen bulk offset");
                    offset_unknown = off < 0;
                    out.offset = off < 0 ? 0 : static_cast<std::uint64_t>(off);
                }
            } else if (flags & (1u << 12))
                type = 4;
            id.replace(22, 2, type == 2 ? "02" : type == 3 ? "03" : "04");
            out.chunk = find(id);
            if (out.chunk == SIZE_MAX)
                out.unavailable = "missing-chunk";
            else if (out.bytes) {
                const auto size = container_.entries()[out.chunk].size;
                need(out.offset <= size && out.bytes <= size - out.offset,
                     "Zen bulk window exceeds external chunk");
            }
        }
        if (!unused && !out.bytes)
            out.unavailable = "empty-payload";
        out.metadata = {{"bulkIndex", index},
                        {"flags", flags},
                        {"offset", (unused || offset_unknown) ? Json(nullptr) : Json(out.offset)},
                        {"declaredOffset", row.at("offset")},
                        {"bytes", out.bytes},
                        {"chunkId", id},
                        {"storage", unused     ? "unused"
                                    : external ? "external"
                                               : "inline"}};
        return out;
    }
    void load(ZenBulkWindow &out) {
        reads_.control.check();
        if (!out.unavailable.empty())
            return;
        std::string hash = member_.source_hash;
        if (!out.data) {
            need(out.chunk < container_.entries().size(), "Missing Zen bulk chunk selection");
            const auto &chunk = container_.entries()[out.chunk];
            hash = chunk.source_hash;
            auto it = chunks_.find(chunk.id);
            if (it == chunks_.end()) {
                if (chunk.size > maximum_ - total_)
                    throw Error(prefix_ + "-limit", "Verified external chunks exceed aggregate read budget");
                total_ += chunk.size;
                it = chunks_.emplace(chunk.id, container_.read(out.chunk, reads_)).first;
            }
            out.data = &it->second;
        }
        need(out.offset <= out.data->size() && out.bytes <= out.data->size() - out.offset,
             "Loaded Zen bulk window exceeds verified bytes");
        out.metadata["sha256"] = hash_range(*out.data, out.offset, out.bytes, reads_.control);
        out.metadata["chunkSourceHash"] = hash;
    }
};
} // namespace cyuediff::detail
