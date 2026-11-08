#include <cyuediff/compressed_curves.hpp>
#include <cyuediff/content_hash.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <limits>
using namespace cyuediff;
namespace {
unsigned checks = 0;
double maximum = 0;
void check(bool v, const char *s) {
    ++checks;
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void refused(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid compressed curve accepted");
}
Json load(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    Json j;
    f >> j;
    return j;
}
CompressedCurveOptions options() {
    CompressedCurveOptions o;
    o.profile = CompressedCurveProfile::unreal_5_3_windows;
    return o;
}
CompressedCurveSource source(const Json &r) {
    CompressedCurveSource s;
    const auto codec = r.at("codecClass").get<std::string>();
    s.codec = codec == "/Script/Engine.AnimCurveCompressionCodec_CompressedRichCurve"
                  ? CompressedCurveCodec::rich_curve
              : codec == "/Script/Engine.AnimCurveCompressionCodec_UniformIndexable"
                  ? CompressedCurveCodec::uniform_indexable
                  : CompressedCurveCodec::uniformly_sampled;
    s.names = r.at("names").get<std::vector<std::string>>();
    s.buffer = r.at("buffer").get<std::vector<std::uint8_t>>();
    return s;
}
void reference(const Json &r) {
    const auto s = source(r);
    const auto a = decode_compressed_curves(s, options());
    check(a.curve_count() == s.names.size(), "Reference curve count");
    const auto catalog = a.catalog();
    check(catalog.at("bufferIoHash") == r.at("bufferIoHash"), "Complete independent buffer hash");
    check(catalog.at("unreferencedBytes") == 0, "Actual compressor buffers fully covered");
    for (std::size_t t = 0; t < r.at("times").size(); ++t) {
        const double time = r.at("times")[t];
        const auto sample = a.sample(time);
        for (std::size_t i = 0; i < s.names.size(); ++i) {
            const auto &row = r.at("samples")[t][i];
            check(row.at("name") == s.names[i] && row.at("batchPresent") == true,
                  "Actual named curve identity");
            const double expected = row.at("batch"), value = a.sample_curve(i, time);
            const double delta = std::abs(value - expected);
            maximum = std::max(maximum, delta);
            if (delta > 3e-5) {
                std::cerr << "codec " << int(s.codec) << " curve " << i << " time " << time << " got "
                          << value << " expected " << expected << " delta " << delta << " format "
                          << catalog.at("curves")[i].at("format") << '\n';
            }
            check(delta <= 3e-5, "Independent actual compressed curve value");
            check(sample.at("values")[i].at("value") == value, "Focused and whole curve samples agree");
        }
    }
    for (std::size_t i = 0; i < s.names.size(); ++i) {
        const auto preview = a.key_preview(i, 2);
        const auto keys = preview.at("keys").get<std::size_t>();
        check(preview.at("points").size() == std::min<std::size_t>(2, keys), "Bounded key preview");
        for (const auto &point : preview.at("points"))
            check(a.stored_key(i, point.at("index").get<std::uint32_t>()) == point,
                  "Exact original key read");
        if (keys > 1)
            check(preview.at("points").back().at("index") == keys - 1, "Original final key preserved");
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        auto r =
            load(argc == 3 && std::string(argv[1]) == "--reference" ? fs::u8path(argv[2])
                                                                    : fs::u8path(CYUEDIFF_CURVE_REFERENCE));
        if (r.contains("cases"))
            for (const auto &row : r.at("cases"))
                reference(row);
        else
            reference(r);
        if (argc != 1) {
            std::cout << checks << " private curve checks; maximum " << maximum << '\n';
            return 0;
        }
        const auto &rows = r.at("cases");
        for (const auto &row : rows) {
            auto s = source(row);
            const auto o = options();
            auto good = decode_compressed_curves(s, o);
            const auto before = good.to_json(true);
            for (std::size_t n = 0; n < s.buffer.size(); ++n) {
                auto truncated = s;
                truncated.buffer.resize(n);
                refused([&] { decode_compressed_curves(truncated, o); });
            }
            auto small = o;
            small.max_output_bytes = 1;
            refused([&] { decode_compressed_curves(s, small); });
            small = o;
            small.max_read_bytes = 1;
            refused([&] { decode_compressed_curves(s, small); });
            small = o;
            small.max_curves = 0;
            refused([&] { decode_compressed_curves(s, small); });
            small = o;
            small.control.cancelled = [] { return true; };
            refused([&] { decode_compressed_curves(s, small); });
            refused([&] { good.sample(-std::numeric_limits<double>::infinity()); });
            refused([&] { good.sample_curve(good.curve_count(), 0); });
            refused([&] { good.stored_key(0, UINT32_MAX); });
            refused([&] { good.key_preview(0, 1); });
            check(good.to_json(true) == before, "Queries preserve immutable model");
            s.buffer.clear();
            s.names.clear();
            check(good.to_json(true) == before, "Caller storage mutation cannot alter model");
        }
        auto s = source(rows.at(0));
        refused([&] { decode_compressed_curves(s, {}); });
        s.names.push_back(s.names[0]);
        refused([&] { decode_compressed_curves(s, options()); });
        s = source(rows.at(0));
        s.names[0] = std::string(1, char(255));
        refused([&] { decode_compressed_curves(s, options()); });
        s = source(rows.at(0));
        s.buffer[0] = 255;
        refused([&] { decode_compressed_curves(s, options()); });
        auto put32 = [](std::vector<std::uint8_t> &bytes, std::size_t at, std::uint32_t v) {
            for (unsigned i = 0; i < 4; ++i)
                bytes.at(at + i) = std::uint8_t(v >> (8 * i));
        };
        for (const auto &row : rows) {
            auto original = source(row);
            auto trailing = original;
            trailing.buffer.insert(trailing.buffer.end(), {7, 8, 9});
            auto extra = decode_compressed_curves(trailing, options());
            check(extra.catalog()["unreferencedBytes"] == 3, "Uninterpreted tail retained and counted");
            check(extra.sample(0) == decode_compressed_curves(original, options()).sample(0),
                  "Tail bytes do not fabricate semantic changes");
            if (original.codec == CompressedCurveCodec::rich_curve && original.buffer[0] >= 2) {
                auto corrupt = original;
                put32(corrupt.buffer, 4, 0xffffffffu);
                refused([&] { decode_compressed_curves(corrupt, options()); });
                corrupt = original;
                put32(corrupt.buffer, 8, 0);
                refused([&] { decode_compressed_curves(corrupt, options()); });
                corrupt = original;
                corrupt.buffer[2] = 255;
                refused([&] { decode_compressed_curves(corrupt, options()); });
                auto bounded = options();
                bounded.max_keys = 1;
                refused([&] { decode_compressed_curves(original, bounded); });
                // Two named curves may share an exact encoded window; partial overlap is refused.
                auto shared = original;
                shared.names.push_back("AnotherOriginalCurve");
                shared.buffer.insert(shared.buffer.begin() + 12, original.buffer.begin(),
                                     original.buffer.begin() + 12);
                put32(shared.buffer, 8, 24);
                put32(shared.buffer, 20, 24);
                auto pair = decode_compressed_curves(shared, options());
                check(pair.curve_count() == 2 && pair.sample_curve(0, .5) == pair.sample_curve(1, .5),
                      "Exact shared rich window accepted");
                check(pair.catalog()["unreferencedBytes"] == 0, "Shared key window counted once");
                shared.buffer.insert(shared.buffer.end(), 4, 0);
                put32(shared.buffer, 20, 28);
                refused([&] { decode_compressed_curves(shared, options()); });
            } else if (original.codec == CompressedCurveCodec::uniformly_sampled) {
                auto corrupt = original;
                put32(corrupt.buffer, 0, std::uint32_t(original.names.size() + 1));
                refused([&] { decode_compressed_curves(corrupt, options()); });
                corrupt = original;
                put32(corrupt.buffer, 4, 0);
                refused([&] { decode_compressed_curves(corrupt, options()); });
                corrupt = original;
                put32(corrupt.buffer, 8, 0x7f800000);
                refused([&] { decode_compressed_curves(corrupt, options()); });
                corrupt = original;
                corrupt.buffer[12] ^= 1;
                refused([&] { decode_compressed_curves(corrupt, options()); });
            }
        }
        for (const auto &row : rows) {
            auto bad = source(row);
            if (bad.codec == CompressedCurveCodec::rich_curve && bad.buffer[0] <= 1)
                put32(bad.buffer, 4, 0x7fc00000u);
            else
                put32(bad.buffer, bad.buffer.size() - 4, 0x7fc00000u);
            refused([&] { decode_compressed_curves(bad, options()); });
        }
        auto text_source = source(rows.at(0));
        text_source.names[0] = std::string(1024 * 1024, '\n');
        auto text_options = options();
        text_options.max_text_bytes = 1024;
        refused([&] { decode_compressed_curves(text_source, text_options); });
        text_options = options();
        text_options.max_output_bytes = 1024;
        refused([&] { decode_compressed_curves(text_source, text_options); });
        auto uniform_metadata = decode_compressed_curves(source(rows.back()), options()).catalog();
        check(uniform_metadata["curves"][0]["constantSamples"] == true,
              "Native constant bitset is explicit metadata");
        check(uniform_metadata["curves"][1]["constantSamples"] == false &&
                  uniform_metadata["curves"][1]["constantValue"].is_null(),
              "Animated sample rows do not invent constant values");
        auto cancellation_model = decode_compressed_curves(source(rows.at(24)), options());
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        refused([&] { cancellation_model.catalog(cancel); });
        refused([&] { cancellation_model.to_json(true, cancel); });
        refused([&] { cancellation_model.key_preview(0, 2, cancel); });
        refused([&] { cancellation_model.stored_key(0, 0, cancel); });
        refused([&] { cancellation_model.sample_curve(0, 0, 0, cancel); });
        refused([&] { cancellation_model.sample(0, 0, cancel); });
        auto plain = decode_compressed_curves(source(rows.at(0)), options());
        check(plain.catalog()["curves"][0]["emptyUsesCallerDefault"] == true,
              "Empty sentinel is explicit in metadata");
        check(plain.sample_curve(0, 0, 7) == 7, "Empty sentinel uses explicit caller default");
        std::cout << checks << " compressed curve checks; maximum independent delta " << maximum << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
