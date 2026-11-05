#include <cyuediff/binary_delta.hpp>
#include <iostream>
#include <random>
using namespace cyuediff;
int main() {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Compact delta check failed");
    };
    auto rejects = [&](auto fn) {
        bool failed = false;
        try {
            fn();
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed);
    };
    try {
        BinaryDeltaOptions o;
        o.block_size = 16;
        std::vector<std::uint8_t> base(64);
        for (std::size_t i = 0; i < base.size(); ++i)
            base[i] = std::uint8_t(i);
        std::vector<std::uint8_t> target(base.begin() + 16, base.begin() + 48);
        target.insert(target.end(), {0, 255, 128});
        BinaryDelta d;
        d.block_size = 16;
        d.base_bytes = base.size();
        d.target_bytes = target.size();
        d.base_sha256 = sha256_bytes(base);
        d.target_sha256 = sha256_bytes(target);
        d.operations = {{BinaryDeltaKind::copy, 16, 32, {}}, {BinaryDeltaKind::literal, 0, 3, {0, 255, 128}}};
        auto bytes = d.to_binary(o);
        check(bytes.size() == 112 + 17 + 9 + 3);
        check(std::string(bytes.begin(), bytes.begin() + 7) == "CYUEDLT" && bytes[7] == 0);
        check(bytes[8] == 1 && bytes[12] == 0 && bytes[16] == 16 && bytes[24] == 64 && bytes[32] == 35 &&
              bytes[40] == 2);
        check(bytes[112] == 0 && bytes[113] == 16 && bytes[121] == 32 && bytes[129] == 1 && bytes[130] == 3);
        check(bytes[138] == 0 && bytes[139] == 255 && bytes[140] == 128);
        auto parsed = BinaryDelta::from_binary(bytes, o);
        check(parsed.to_json(o) == d.to_json(o) && parsed.to_binary(o) == bytes);
        check(apply_binary_delta(base, parsed, o) == target);
        for (std::size_t n = 0; n < bytes.size(); ++n)
            rejects([&] { BinaryDelta::from_binary({bytes.begin(), bytes.begin() + n}, o); });
        for (auto at : {0, 7, 8, 12, 16, 20, 24, 32, 40, 112, 113, 121, 129, 130}) {
            auto bad = bytes;
            bad[at] = (at == 16 || at == 24) ? 0 : 255;
            rejects([&] { BinaryDelta::from_binary(bad, o); });
        }
        auto bad = bytes;
        bad.push_back(0);
        rejects([&] { BinaryDelta::from_binary(bad, o); });
        bad = bytes;
        bad[80] ^= 1;
        auto identity = BinaryDelta::from_binary(bad, o);
        rejects([&] { apply_binary_delta(base, identity, o); });
        bad = bytes;
        bad.back() ^= 1;
        identity = BinaryDelta::from_binary(bad, o);
        rejects([&] { apply_binary_delta(base, identity, o); });
        auto limit = o;
        limit.max_manifest_bytes = bytes.size();
        check(d.to_binary(limit) == bytes &&
              BinaryDelta::from_binary(bytes, limit).to_binary(limit) == bytes);
        --limit.max_manifest_bytes;
        rejects([&] { d.to_binary(limit); });
        rejects([&] { BinaryDelta::from_binary(bytes, limit); });
        limit = o;
        limit.max_literal_bytes = 2;
        rejects([&] { BinaryDelta::from_binary(bytes, limit); });
        limit = o;
        limit.max_input_bytes = 63;
        rejects([&] { BinaryDelta::from_binary(bytes, limit); });
        limit = o;
        limit.max_operations = 1;
        rejects([&] { BinaryDelta::from_binary(bytes, limit); });
        limit = o;
        limit.control.cancelled = [] { return true; };
        rejects([&] { BinaryDelta::from_binary(bytes, limit); });
        rejects([&] { d.to_binary(limit); });
        const auto empty = make_binary_delta({}, {}, o);
        check(empty.to_binary(o).size() == 112 &&
              BinaryDelta::from_binary(empty.to_binary(o), o).operations.empty());
        limit = o;
        limit.max_manifest_bytes = 112;
        check(empty.to_binary(limit).size() == 112);
        limit.max_manifest_bytes = 111;
        rejects([&] { empty.to_binary(limit); });
        std::mt19937 rng(163);
        for (unsigned i = 0; i < 200; ++i) {
            std::vector<std::uint8_t> a(256 + i);
            for (auto &b : a)
                b = std::uint8_t(rng());
            auto b = a;
            b.insert(b.begin() + rng() % b.size(), {0, 255, 37});
            if (i % 2)
                b.erase(b.begin() + 13, b.begin() + 23);
            const auto plan = make_binary_delta(a, b, o);
            const auto wire = plan.to_binary(o);
            const auto recovered = BinaryDelta::from_binary(wire, o);
            check(recovered.to_json(o) == plan.to_json(o));
            check(apply_binary_delta(a, recovered, o) == b);
        }
        std::cout << checks << " compact wire checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
