#include <cyuediff/binary_delta.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
using namespace cyuediff;
int main() {
    int checks = 0;
    const auto root =
        fs::current_path() / (".test-tmp-binary-format-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Delta format check failed");
    };
    auto rejects = [&](auto action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed);
    };
    try {
        BinaryDeltaOptions o;
        o.block_size = 16;
        std::mt19937 rng(18);
        std::vector<std::uint8_t> base(1024);
        for (auto &b : base)
            b = std::uint8_t(rng());
        auto target = base;
        target.insert(target.begin() + 3, {0, 255, 19});
        target[601] ^= 4;
        const auto d = make_binary_delta(base, target, o);
        const auto j = d.to_json(o);
        auto parsed = BinaryDelta::parse(j.dump(), o);
        check(parsed.to_json(o) == j && apply_binary_delta(base, parsed, o) == target);
        check(apply_binary_delta({}, make_binary_delta({}, {}, o), o).empty());
        check(apply_binary_delta({}, make_binary_delta({}, target, o), o) == target);
        check(apply_binary_delta(base, make_binary_delta(base, {}, o), o).empty());
        auto wrong = base;
        wrong[500] ^= 1;
        rejects([&] { apply_binary_delta(wrong, parsed, o); });
        wrong.push_back(0);
        rejects([&] { apply_binary_delta(wrong, parsed, o); });
        auto bad = d;
        bad.target_sha256 = std::string(64, '0');
        rejects([&] { apply_binary_delta(base, bad, o); });
        bad = d;
        bad.operations.front().data.front() ^= 1;
        rejects([&] { apply_binary_delta(base, bad, o); });
        auto invalid = [&](Json value) { rejects([&] { BinaryDelta::from_json(value, o); }); };
        auto v = j;
        v["schema"] = "future";
        invalid(v);
        v = j;
        v["extra"] = true;
        invalid(v);
        v = j;
        v["blockSize"] = 16.0;
        invalid(v);
        v = j;
        v["base"]["bytes"] = -1;
        invalid(v);
        v = j;
        v["target"]["bytes"] = 18446744073709551615ull;
        invalid(v);
        v = j;
        v["base"]["sha256"] = "wrong";
        invalid(v);
        v = j;
        v["operations"][0] = {{"kind", "literal"}, {"hex", "Ff"}};
        invalid(v);
        v = j;
        v["operations"][0]["hex"] = "a";
        invalid(v);
        v = j;
        v["operations"][0]["hex"] = "";
        invalid(v);
        v = j;
        v["operations"][0] = {{"kind", "copy"}, {"offset", 0}, {"bytes", 0}};
        invalid(v);
        v = j;
        v["operations"][0] = {{"kind", "copy"}, {"offset", 1020}, {"bytes", 100}};
        invalid(v);
        v = j;
        v["operations"][0]["kind"] = "run";
        invalid(v);
        rejects([&] { BinaryDelta::parse("{\"schema\":\"future\"," + j.dump().substr(1), o); });
        rejects([&] { BinaryDelta::parse(std::string(10, '[') + "0" + std::string(10, ']'), o); });
        auto limit = o;
        limit.max_manifest_bytes = 10;
        rejects([&] { BinaryDelta::parse(j.dump(), limit); });
        rejects([&] { d.to_json(limit); });
        limit = o;
        limit.max_operations = 1;
        rejects([&] { BinaryDelta::from_json(j, limit); });
        limit = o;
        limit.max_literal_bytes = 0;
        rejects([&] { BinaryDelta::from_json(j, limit); });
        limit = o;
        limit.control.cancelled = [] { return true; };
        rejects([&] { apply_binary_delta(base, d, limit); });
        rejects([&] { BinaryDelta::parse(j.dump(), limit); });
        // Mutate all source bytes in a small base; identity verification must reject every wrong base.
        for (std::size_t i = 0; i < 64; ++i) {
            wrong = base;
            wrong[i] ^= 1;
            rejects([&] { apply_binary_delta(wrong, d, o); });
        }
        fs::create_directories(root);
        auto a = root / fs::u8path("avant-é.bin"), b = root / "after.bin", manifest = root / "delta.json";
        auto write = [](const fs::path &p, const std::vector<std::uint8_t> &v) {
            std::ofstream f(p, std::ios::binary);
            f.write(reinterpret_cast<const char *>(v.data()), v.size());
        };
        write(a, base);
        write(b, target);
        const auto text = j.dump();
        write(manifest, {text.begin(), text.end()});
        check(make_binary_delta_files(a, b, o).to_json(o) == j);
        check(read_binary_delta(manifest, o).to_json(o) == j);
        check(read_binary_file(a, base.size()) == base);
        rejects([&] { read_binary_file(a, 1); });
        rejects([&] { read_binary_file(root / "absent", 100); });
        check(sha256_file(a) == d.base_sha256 && sha256_file(b) == d.target_sha256);
        fs::remove_all(root);
        std::cout << checks << " binary delta format/reconstruction checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
}
