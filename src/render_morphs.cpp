#include <cyuediff/render_morphs.hpp>
#include <set>
#include <algorithm>
namespace cyuediff {
MeshRenderMorphs::MeshRenderMorphs(std::shared_ptr<const CompressedMorphBuffer> buffer,
                                   std::vector<std::string> names, std::string unavailable)
    : buffer_(std::move(buffer)), names_(std::move(names)), unavailable_(std::move(unavailable)) {
    if (!buffer_)
        throw Error("render-morphs", "A render morph binding requires a native buffer");
    if (!ready()) {
        names_.clear();
        return;
    }
    if (names_.size() != buffer_->morph_count())
        throw Error("render-morphs", "Morph names do not match native table cardinality");
    std::set<std::string> unique;
    for (auto name : names_) {
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c); });
        if (name.empty() || !unique.insert(name).second)
            throw Error("render-morphs", "Empty or duplicate native morph identity");
    }
}
Json MeshRenderMorphs::semantic_snapshot(const OperationControl &c) const {
    Json out = {{"schema", "cyuediff.render-morphs/1"},
                {"ready", ready()},
                {"scope", "stored-render-deltas"},
                {"targets", Json::object()}};
    if (!ready()) {
        out["unavailable"] = unavailable_;
        return out;
    }
    for (std::size_t mi = 0; mi < names_.size(); ++mi) {
        c.check();
        auto &vertices = out["targets"][names_[mi]]["vertices"] = Json::object();
        for (std::uint64_t di = 0; di < buffer_->delta_count(mi); ++di) {
            if (!(di & 255))
                c.checkpoint("render-morph-snapshot", di, buffer_->delta_count(mi));
            const auto d = buffer_->delta(mi, di, c);
            // The compression batches may change without changing a stored value.
            if (d.quantized_position == std::array<std::int32_t, 3>{} &&
                d.quantized_normal == std::array<std::int32_t, 3>{})
                continue;
            auto &entries = vertices[std::to_string(d.vertex)];
            if (entries.is_null())
                entries = Json::array();
            entries.push_back({{"position", d.position}, {"normal", d.normal}});
        }
    }
    return out;
}
Json MeshRenderMorphs::to_json(bool include, const OperationControl &c) const {
    Json out = {{"schema", "cyuediff.render-morphs/1"}, {"ready", ready()},
                {"scope", "stored-render-deltas"},      {"unavailable", unavailable_},
                {"buffer", buffer_->information(c)},    {"targets", Json::array()}};
    for (std::size_t mi = 0; mi < buffer_->morph_count(); ++mi) {
        auto row = buffer_->morph_information(mi, c);
        row["name"] = ready() ? Json(names_[mi]) : Json(nullptr);
        if (include) {
            row["deltas"] = Json::array();
            for (std::uint64_t di = 0; di < buffer_->delta_count(mi); ++di) {
                if (!(di & 255))
                    c.checkpoint("render-morph-details", di, buffer_->delta_count(mi));
                row["deltas"].push_back(buffer_->delta(mi, di, c).to_json());
            }
        }
        out["targets"].push_back(std::move(row));
    }
    return out;
}
} // namespace cyuediff
