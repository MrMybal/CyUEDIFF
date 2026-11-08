#pragma once
#include <cyuediff/compressed_morphs.hpp>
namespace cyuediff {
// Named stored render deltas. Missing entries are zero. Repeated entries for a
// vertex retain their order and multiplicity; no blend or skin evaluation occurs.
// Unknown bindings stay unavailable and cannot become fabricated removals.
class MeshRenderMorphs {
    std::shared_ptr<const CompressedMorphBuffer> buffer_;
    std::vector<std::string> names_;
    std::string unavailable_;

  public:
    MeshRenderMorphs(std::shared_ptr<const CompressedMorphBuffer>, std::vector<std::string> names,
                     std::string unavailable = {});
    bool ready() const {
        return unavailable_.empty();
    }
    const std::string &unavailable() const {
        return unavailable_;
    }
    const std::vector<std::string> &names() const {
        return names_;
    }
    const CompressedMorphBuffer &buffer() const {
        return *buffer_;
    }
    // Stable identities and decoded values only; excludes packing, bounds and hashes.
    Json semantic_snapshot(const OperationControl & = {}) const;
    Json to_json(bool include_deltas = false, const OperationControl & = {}) const;
};
} // namespace cyuediff
