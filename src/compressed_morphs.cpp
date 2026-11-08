#include <cyuediff/compressed_morphs.hpp>
#include "archive.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace cyuediff {
namespace {
void need(bool value, const char *message) {
    if (!value)
        throw Error("compressed-morph-layout", message);
}
void limit(bool value, const char *message) {
    if (!value)
        throw Error("compressed-morph-limit", message);
}
struct Reader {
    const std::vector<std::uint8_t> &bytes;
    std::uint64_t at, end;
    void range(std::uint64_t n) const {
        need(at <= end && n <= end - at, "Morph field exceeds native window");
    }
    std::uint32_t u32() {
        range(4);
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= std::uint32_t(bytes[at++]) << (8 * i);
        return value;
    }
    std::uint32_t count(std::uint32_t cap) {
        auto v = u32();
        need(v <= INT32_MAX, "Negative morph array count");
        limit(v <= cap, "Morph array exceeds count budget");
        return v;
    }
    float f32() {
        const auto bits = u32();
        float value;
        std::memcpy(&value, &bits, 4);
        need(std::isfinite(value), "Non-finite morph float");
        return value;
    }
};
std::int32_t signed_word(std::uint32_t v) {
    std::int32_t out;
    std::memcpy(&out, &v, 4);
    return out;
}
struct Batch {
    std::uint32_t offset = 0, count = 0, index_bits = 0, index_min = 0, stride = 0;
    std::array<std::uint32_t, 3> position_bits{}, normal_bits{};
    std::array<std::int32_t, 3> position_min{}, normal_min{};
    bool normals = false;
    std::uint64_t first_delta = 0;
};
struct Morph {
    std::array<float, 4> low{}, high{};
    std::uint32_t first = 0, batches = 0;
    std::uint64_t deltas = 0, duplicates = 0;
};
std::uint32_t bits(const std::vector<std::uint32_t> &words, std::uint64_t &at, unsigned width) {
    if (!width)
        return 0;
    const auto word = at / 32;
    const unsigned shift = unsigned(at % 32);
    need(word < words.size(), "Morph bit field exceeds data");
    std::uint64_t value = words[std::size_t(word)] >> shift;
    if (shift + width > 32) {
        need(word + 1 < words.size(), "Morph bit field crosses data boundary");
        value |= std::uint64_t(words[std::size_t(word + 1)]) << (32 - shift);
    }
    at += width;
    return static_cast<std::uint32_t>(value & ((1ull << width) - 1));
}
CompressedMorphDelta decode(const std::vector<std::uint32_t> &words, const Batch &b, unsigned local, float pp,
                            float np, std::uint32_t vertices) {
    need(local < b.count, "Morph delta index exceeds batch");
    auto at = std::uint64_t(b.offset) * 8 + std::uint64_t(local) * b.stride;
    CompressedMorphDelta out;
    const auto vertex = std::uint64_t(b.index_min) + local + bits(words, at, b.index_bits);
    need(vertex < vertices, "Morph delta references absent render vertex");
    out.vertex = static_cast<std::uint32_t>(vertex);
    for (unsigned k = 0; k < 3; ++k) {
        const auto q = std::int64_t(b.position_min[k]) + bits(words, at, b.position_bits[k]);
        need(q >= -134217728 && q <= 134217727, "Morph position exceeds native quantization range");
        out.quantized_position[k] = static_cast<std::int32_t>(q);
        out.position[k] = static_cast<float>(q) * pp;
        need(std::isfinite(out.position[k]), "Non-finite decoded morph position");
    }
    out.normal_stored = b.normals;
    if (b.normals)
        for (unsigned k = 0; k < 3; ++k) {
            const auto q = std::int64_t(b.normal_min[k]) + bits(words, at, b.normal_bits[k]);
            need(q >= -32768 && q <= 32767, "Morph normal exceeds native quantization range");
            out.quantized_normal[k] = static_cast<std::int32_t>(q);
            out.normal[k] = static_cast<float>(q) * np;
            need(std::isfinite(out.normal[k]), "Non-finite decoded morph normal");
        }
    return out;
}
} // namespace
struct CompressedMorphBuffer::Impl {
    std::vector<std::uint32_t> words;
    std::vector<Batch> batches;
    std::vector<Morph> morphs;
    std::uint32_t vertices = 0;
    std::uint64_t bytes = 0, accounted = 0, total_deltas = 0;
    float pp = 0, np = 0;
    std::string sha, data_sha;
};
CompressedMorphBuffer::CompressedMorphBuffer(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
CompressedMorphBuffer CompressedMorphBuffer::parse(const std::vector<std::uint8_t> &bytes,
                                                   std::uint32_t vertices, const CompressedMorphOptions &o) {
    auto model = from_native_window(bytes, 0, bytes.size(), vertices, o);
    need(model.consumed_bytes() == bytes.size(), "Trailing bytes after native morph serialization");
    return model;
}
CompressedMorphBuffer CompressedMorphBuffer::from_native_window(const std::vector<std::uint8_t> &bytes,
                                                                std::uint64_t offset, std::uint64_t end,
                                                                std::uint32_t vertices,
                                                                const CompressedMorphOptions &o) {
    if (o.profile != MorphCompressionProfile::unreal_5_3_windows)
        throw Error("compressed-morph-profile", "Select the UE 5.3 Windows morph compression profile");
    o.control.check();
    need(offset <= end && end <= bytes.size(), "Invalid native morph window");
    limit(vertices <= o.max_vertices, "Morph base vertices exceed budget");
    Reader r{bytes, offset, end};
    auto p = std::make_shared<Impl>();
    p->vertices = vertices;
    auto reserve = [&](std::uint64_t n) {
        limit(p->accounted <= o.max_output_bytes && n <= o.max_output_bytes - p->accounted,
              "Morph output exceeds byte budget");
        p->accounted += n;
    };
    reserve(sizeof(Impl) + 256);
    const auto nw = r.count(INT32_MAX);
    limit(std::uint64_t(nw) * 4 <= o.max_data_bytes, "Morph data exceeds byte budget");
    r.range(std::uint64_t(nw) * 4);
    reserve(std::uint64_t(nw) * 4);
    const auto data_at = r.at;
    p->words.reserve(nw);
    for (unsigned i = 0; i < nw; ++i) {
        if (!(i & 4095))
            o.control.check();
        p->words.push_back(r.u32());
    }
    const auto count = r.count(o.max_morphs);
    r.range(std::uint64_t(count) * 16);
    reserve(std::uint64_t(count) * sizeof(Morph));
    p->morphs.resize(count);
    for (auto &m : p->morphs) {
        o.control.check();
        for (auto &v : m.low)
            v = r.f32();
    }
    need(r.count(o.max_morphs) == count, "Morph maximum table count disagrees");
    r.range(std::uint64_t(count) * 16);
    for (auto &m : p->morphs) {
        o.control.check();
        for (unsigned k = 0; k < 4; ++k) {
            m.high[k] = r.f32();
            need(m.low[k] <= m.high[k], "Morph source extrema are reversed");
        }
    }
    need(r.count(o.max_morphs) == count, "Morph batch-offset table count disagrees");
    r.range(std::uint64_t(count) * 4);
    for (auto &m : p->morphs)
        m.first = r.u32();
    need(r.count(o.max_morphs) == count, "Morph batch-count table count disagrees");
    r.range(std::uint64_t(count) * 4);
    for (auto &m : p->morphs)
        m.batches = r.u32();
    const auto nb = r.u32();
    limit(nb <= o.max_batches, "Morph batches exceed budget");
    p->pp = r.f32();
    p->np = r.f32();
    need(count ? (p->pp > 0 && p->np > 0) : (p->pp >= 0 && p->np >= 0), "Invalid native morph precision");
    need(nb ? nw >= std::uint64_t(nb) * 10 + 3 : nw == 0, "Morph data/header count disagrees");
    std::uint64_t covered = 0;
    for (const auto &m : p->morphs) {
        need(m.first == covered && m.batches <= std::uint64_t(nb) - covered,
             "Morph ranges do not partition batch table");
        covered += m.batches;
    }
    need(covered == nb, "Unowned native morph batches");
    reserve(std::uint64_t(nb) * sizeof(Batch));
    p->batches.reserve(nb);
    const auto header_bytes = std::uint64_t(nb) * 40;
    for (unsigned i = 0; i < nb; ++i) {
        o.control.checkpoint("compressed-morph-batches", i, nb);
        const auto at = std::size_t(i) * 10;
        Batch b;
        b.offset = p->words[at];
        auto flags = p->words[at + 1];
        b.index_bits = flags & 31;
        b.normals = (flags & (1u << 20)) != 0;
        b.count = flags >> 21;
        b.index_min = p->words[at + 2];
        need(b.count >= 1 && b.count <= 64, "Invalid native morph batch size");
        auto normal_flags = p->words[at + 6];
        need((normal_flags >> 15) == 0, "Unknown native morph normal flags");
        b.stride = b.index_bits;
        for (unsigned k = 0; k < 3; ++k) {
            b.position_bits[k] = (flags >> (5 + 5 * k)) & 31;
            b.position_min[k] = signed_word(p->words[at + 3 + k]);
            b.normal_bits[k] = (normal_flags >> (5 * k)) & 31;
            b.normal_min[k] = signed_word(p->words[at + 7 + k]);
            need(b.position_bits[k] <= 28 && b.normal_bits[k] <= 16,
                 "Morph component bit width exceeds profile");
            need(b.normals || (b.normal_bits[k] == 0 && b.normal_min[k] == 0),
                 "Normal-less morph batch contains normal fields");
            b.stride += b.position_bits[k] + (b.normals ? b.normal_bits[k] : 0);
        }
        const auto stream_words = (std::uint64_t(b.count) * b.stride + 31) / 32;
        need((b.offset & 3) == 0 && b.offset >= header_bytes &&
                 std::uint64_t(b.offset) / 4 + stream_words + 3 <= nw,
             "Morph batch exceeds padded data window");
        limit(p->total_deltas <= o.max_deltas && b.count <= o.max_deltas - p->total_deltas,
              "Morph deltas exceed aggregate budget");
        p->total_deltas += b.count;
        p->batches.push_back(b);
    }
    std::uint64_t validated = 0;
    for (unsigned mi = 0; mi < count; ++mi) {
        auto &m = p->morphs[mi];
        bool first = true;
        std::uint32_t previous = 0;
        for (std::uint32_t bi = m.first; bi < std::uint64_t(m.first) + m.batches; ++bi) {
            auto &b = p->batches[bi];
            b.first_delta = m.deltas;
            m.deltas += b.count;
            for (unsigned di = 0; di < b.count; ++di) {
                if (!(validated & 255))
                    o.control.checkpoint("compressed-morph-deltas", validated, p->total_deltas);
                ++validated;
                const auto d = decode(p->words, b, di, p->pp, p->np, vertices);
                need(first || d.vertex >= previous, "Morph render vertex indices are not sorted");
                if (!first && d.vertex == previous)
                    ++m.duplicates;
                previous = d.vertex;
                first = false;
            }
        }
    }
    p->bytes = r.at - offset;
    p->sha = detail::hash_range(bytes, offset, p->bytes, o.control);
    p->data_sha = detail::hash_range(bytes, data_at, std::uint64_t(nw) * 4, o.control);
    o.control.check();
    return CompressedMorphBuffer(std::move(p));
}
std::size_t CompressedMorphBuffer::morph_count() const {
    return impl_->morphs.size();
}
std::size_t CompressedMorphBuffer::batch_count() const {
    return impl_->batches.size();
}
std::uint64_t CompressedMorphBuffer::delta_count(std::size_t i) const {
    need(i < morph_count(), "Morph selection exceeds table");
    return impl_->morphs[i].deltas;
}
std::uint64_t CompressedMorphBuffer::consumed_bytes() const {
    return impl_->bytes;
}
std::uint64_t CompressedMorphBuffer::accounted_output_bytes() const {
    return impl_->accounted;
}
float CompressedMorphBuffer::position_precision() const {
    return impl_->pp;
}
float CompressedMorphBuffer::normal_precision() const {
    return impl_->np;
}
Json CompressedMorphBuffer::information(const OperationControl &c) const {
    c.check();
    return {{"schema", "cyuediff.compressed-morphs/1"},
            {"profile", "ue5.3-windows"},
            {"morphs", morph_count()},
            {"batches", batch_count()},
            {"storedDeltas", impl_->total_deltas},
            {"baseVertices", impl_->vertices},
            {"positionPrecision", impl_->pp},
            {"normalPrecision", impl_->np},
            {"nativeBytes", impl_->bytes},
            {"nativeSha256", impl_->sha},
            {"dataBytes", impl_->words.size() * 4},
            {"dataSha256", impl_->data_sha},
            {"accountedOutputBytes", impl_->accounted},
            {"scope", "stored render deltas; no names, blend weights or normal recomputation"}};
}
Json CompressedMorphBuffer::morph_information(std::size_t i, const OperationControl &c) const {
    c.check();
    need(i < morph_count(), "Morph selection exceeds table");
    const auto &m = impl_->morphs[i];
    return {{"index", i},
            {"firstBatch", m.first},
            {"batches", m.batches},
            {"storedDeltas", m.deltas},
            {"duplicateVertices", m.duplicates},
            {"sourceMinimum", m.low},
            {"sourceMaximum", m.high}};
}
Json CompressedMorphBuffer::batch_information(std::size_t i, const OperationControl &c) const {
    c.check();
    need(i < batch_count(), "Batch selection exceeds table");
    const auto &b = impl_->batches[i];
    return {{"index", i},
            {"dataOffset", b.offset},
            {"elements", b.count},
            {"indexBits", b.index_bits},
            {"indexMinimum", b.index_min},
            {"positionBits", b.position_bits},
            {"positionMinimum", b.position_min},
            {"normalBits", b.normal_bits},
            {"normalMinimum", b.normal_min},
            {"normalsStored", b.normals},
            {"strideBits", b.stride},
            {"firstDelta", b.first_delta}};
}
CompressedMorphDelta CompressedMorphBuffer::delta(std::size_t mi, std::uint64_t entry,
                                                  const OperationControl &c) const {
    c.check();
    need(mi < morph_count(), "Morph selection exceeds table");
    const auto &m = impl_->morphs[mi];
    need(entry < m.deltas, "Delta selection exceeds morph");
    auto first = impl_->batches.begin() + m.first, last = first + m.batches;
    auto found = std::upper_bound(first, last, entry,
                                  [](std::uint64_t value, const Batch &b) { return value < b.first_delta; });
    need(found != first, "Missing morph delta batch");
    --found;
    return decode(impl_->words, *found, static_cast<unsigned>(entry - found->first_delta), impl_->pp,
                  impl_->np, impl_->vertices);
}
Json CompressedMorphDelta::to_json() const {
    return {{"vertex", vertex},
            {"quantizedPosition", quantized_position},
            {"quantizedNormal", quantized_normal},
            {"position", position},
            {"normal", normal},
            {"normalStored", normal_stored}};
}
} // namespace cyuediff
