#include <cyuediff/compressed_morphs.hpp>
#include "compressed_morph_fixture.hpp"
#include <iostream>
#include <fstream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool v, const char *m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void refused(F run, const char *code) {
    try {
        run();
    } catch (const Error &e) {
        check(e.code == code, (std::string("Wrong error ") + e.code + ": " + e.what()).c_str());
        return;
    }
    throw std::runtime_error("Invalid morph input accepted");
}
} // namespace
int main(int argc, char **argv) {
    try {
        CompressedMorphOptions o;
        o.profile = MorphCompressionProfile::unreal_5_3_windows;
        if (argc == 4 && std::string(argv[1]) == "--read-native") {
            std::ifstream in(fs::u8path(argv[2]), std::ios::binary);
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
            check(bool(in), "Native fixture read");
            auto m = CompressedMorphBuffer::parse(bytes, static_cast<std::uint32_t>(std::stoul(argv[3])), o);
            Json j = {
                {"information", m.information()}, {"morphs", Json::array()}, {"batches", Json::array()}};
            for (std::size_t i = 0; i < m.morph_count(); ++i) {
                auto row = m.morph_information(i);
                row["deltas"] = Json::array();
                for (std::uint64_t k = 0; k < m.delta_count(i); ++k)
                    row["deltas"].push_back(m.delta(i, k).to_json());
                j["morphs"].push_back(std::move(row));
            }
            for (std::size_t i = 0; i < m.batch_count(); ++i)
                j["batches"].push_back(m.batch_information(i));
            std::cout << j.dump();
            return 0;
        }
        std::vector<std::vector<MorphFixtureDelta>> targets(5);
        for (unsigned i = 0; i < 140; ++i)
            targets[0].push_back(
                {i * 3, {int(i % 17) - 8, int(i % 5) * 11, -900}, {i / 64 % 2 ? 0 : 16, 0, 0}});
        targets[1] = {{9, {134217720, -134217728, 0}, {32767, -32768, 0}},
                      {31, {-134217728, 134217720, 0}, {-32768, 32767, 0}}};
        targets[2] = {{0, {5, 0, -3}, {}}, {1, {5, 0, -3}, {}}};
        targets[3] = {{8, {1, 0, 0}, {}}, {8, {2, 0, 0}, {}}};
        auto f = compressed_morph_fixture(targets);
        auto model = CompressedMorphBuffer::parse(f.data, 500, o);
        check(model.morph_count() == 5 && model.batch_count() == 6,
              "Sparse, empty and duplicate target inventory retained");
        check(model.morph_information(3)["duplicateVertices"] == 1 && model.delta_count(4) == 0,
              "Duplicate raw entries preserve multiplicity and empty targets stay empty");
        for (std::size_t i = 0; i < targets.size(); ++i) {
            check(model.delta_count(i) == targets[i].size(), "Native entry cardinality");
            for (std::size_t j = 0; j < targets[i].size(); ++j) {
                const auto &d = targets[i][j];
                auto value = model.delta(i, j);
                check(value.vertex == d.vertex && value.quantized_position == d.position &&
                          value.quantized_normal == d.normal,
                      "Original packed indices/position/normal deltas");
                for (unsigned k = 0; k < 3; ++k)
                    check(value.position[k] == float(d.position[k]) * model.position_precision() &&
                              value.normal[k] == float(d.normal[k]) * model.normal_precision(),
                          "Native float32 scaling");
            }
        }
        check(model.batch_information(4)["strideBits"] == 0,
              "Zero-width constant deltas still decode by index minimum plus lane");
        auto saved = model.delta(0, 0).to_json();
        f.data.assign(f.data.size(), 0);
        check(model.delta(0, 0).to_json() == saved, "Immutable model owns all parsed words");
        f = compressed_morph_fixture(targets);
        for (std::size_t end = 0; end < f.data.size(); ++end) {
            auto prefix = f.data;
            prefix.resize(end);
            refused([&] { CompressedMorphBuffer::parse(prefix, 500, o); }, "compressed-morph-layout");
        }
        auto embedded = f;
        embedded.data.insert(embedded.data.begin(), 7, 0xab);
        embedded.zeros(4);
        auto window =
            CompressedMorphBuffer::from_native_window(embedded.data, 7, embedded.data.size() - 4, 500, o);
        check(window.consumed_bytes() == f.data.size() &&
                  window.delta(1, 1).to_json() == model.delta(1, 1).to_json(),
              "Exact bounded native subwindow with caller-owned prefix/suffix");
        for (unsigned bad = 0; bad < 9; ++bad) {
            auto v = f;
            switch (bad) {
            case 0:
                v.patch(4, 0);
                break;
            case 1:
                v.patch(8, 0);
                break;
            case 2:
                v.patch(8, 65 << 21);
                break;
            case 3:
                v.patch(8, 1 << 21 | 29 << 5);
                break;
            case 4:
                v.patch(12, 500);
                break;
            case 5:
                v.patch(28, 1 << 15);
                break;
            case 6:
                v.patch(4, 0x7ffffffc);
                break;
            case 7:
                v.patch(4, 41);
                break;
            case 8:
                v.data.push_back(0);
                break;
            }
            refused([&] { CompressedMorphBuffer::parse(v.data, 500, o); }, "compressed-morph-layout");
        }
        for (unsigned mode = 0; mode < 6; ++mode) {
            auto opts = o;
            switch (mode) {
            case 0:
                opts.max_vertices = 499;
                break;
            case 1:
                opts.max_morphs = 4;
                break;
            case 2:
                opts.max_batches = 5;
                break;
            case 3:
                opts.max_deltas = 1;
                break;
            case 4:
                opts.max_data_bytes = 1;
                break;
            case 5:
                opts.max_output_bytes = 1;
                break;
            }
            refused([&] { CompressedMorphBuffer::parse(f.data, 500, opts); }, "compressed-morph-limit");
        }
        auto invalid = o;
        invalid.profile = MorphCompressionProfile::unspecified;
        refused([&] { CompressedMorphBuffer::parse(f.data, 500, invalid); }, "compressed-morph-profile");
        for (const auto *phase : {"compressed-morph-batches", "compressed-morph-deltas"}) {
            auto options = o;
            bool cancel = false;
            options.control.cancelled = [&] { return cancel; };
            options.control.progress = [&](const auto &p) {
                if (p.phase == phase)
                    cancel = true;
            };
            refused([&] { CompressedMorphBuffer::parse(f.data, 500, options); }, "cancelled");
        }
        refused([&] { model.delta(5, 0); }, "compressed-morph-layout");
        refused([&] { model.delta(0, 140); }, "compressed-morph-layout");
        refused([&] { model.delta(4, 0); }, "compressed-morph-layout");
        auto empty = compressed_morph_fixture({});
        auto m = CompressedMorphBuffer::parse(empty.data, 0, o);
        check(m.morph_count() == 0 && m.batch_count() == 0, "Verified empty native buffer");
        std::cout << checks << " compressed morph checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
