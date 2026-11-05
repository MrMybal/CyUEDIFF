#include <cyuediff/binary_delta.hpp>
#include <iostream>
#include <algorithm>
#include <random>
using namespace cyuediff;
int main() {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Composition check failed");
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
        std::mt19937 rng(164);
        auto mutate = [&](std::vector<std::uint8_t> a) {
            const auto at = rng() % (a.size() + 1);
            a.insert(a.begin() + at, {0, 255, 17, 93, 0});
            if (a.size() > 20) {
                const auto cut = rng() % (a.size() - 10);
                a.erase(a.begin() + cut, a.begin() + cut + 7);
            }
            if (rng() % 2)
                std::reverse(a.begin(), a.end());
            return a;
        };
        for (unsigned i = 0; i < 150; ++i) {
            std::vector<std::uint8_t> a(256 + rng() % 512);
            for (auto &b : a)
                b = std::uint8_t(rng());
            auto b = mutate(a), c = mutate(b), d = mutate(c);
            const auto ab = make_binary_delta(a, b, o), bc = make_binary_delta(b, c, o),
                       cd = make_binary_delta(c, d, o);
            const auto ac = compose_binary_deltas(ab, bc, o);
            check(apply_binary_delta(a, ac, o) == c);
            check(ac.base_sha256 == ab.base_sha256 && ac.target_sha256 == bc.target_sha256);
            check(BinaryDelta::from_binary(ac.to_binary(o), o).to_json(o) == ac.to_json(o));
            const auto left = compose_binary_deltas(ac, cd, o);
            const auto right = compose_binary_deltas(ab, compose_binary_deltas(bc, cd, o), o);
            check(apply_binary_delta(a, left, o) == d && apply_binary_delta(a, right, o) == d);
            check(compose_binary_deltas(ab, make_binary_delta(b, b, o), o).to_json(o) == ab.to_json(o));
        }
        std::vector<std::uint8_t> a(64);
        for (std::size_t i = 0; i < a.size(); ++i)
            a[i] = std::uint8_t(i);
        auto b = a;
        b.insert(b.begin() + 32, {255, 128, 0});
        BinaryDelta ab = make_binary_delta(a, b, o);
        std::vector<std::uint8_t> c(b.begin() + 25, b.begin() + 44);
        c.insert(c.end(), b.begin(), b.begin() + 18);
        c.insert(c.end(), b.begin() + 30, b.begin() + 40);
        BinaryDelta bc;
        bc.block_size = 16;
        bc.base_bytes = b.size();
        bc.target_bytes = c.size();
        bc.base_sha256 = sha256_bytes(b);
        bc.target_sha256 = sha256_bytes(c);
        bc.operations = {{BinaryDeltaKind::copy, 25, 19, {}},
                         {BinaryDeltaKind::copy, 0, 18, {}},
                         {BinaryDeltaKind::copy, 30, 10, {}}};
        check(apply_binary_delta(a, compose_binary_deltas(ab, bc, o), o) == c);
        auto bad = bc;
        bad.base_sha256 = std::string(64, '0');
        rejects([&] { compose_binary_deltas(ab, bad, o); });
        bad = bc;
        ++bad.base_bytes;
        rejects([&] { compose_binary_deltas(ab, bad, o); });
        bad = bc;
        bad.operations[0].offset = 1000;
        rejects([&] { compose_binary_deltas(ab, bad, o); });
        auto limit = o;
        limit.max_operations = 3;
        rejects([&] { compose_binary_deltas(ab, bc, limit); });
        limit = o;
        limit.max_composition_fragments = 1;
        rejects([&] { compose_binary_deltas(ab, bc, limit); });
        limit = o;
        limit.max_composition_fragments = 0;
        rejects([&] { compose_binary_deltas(ab, bc, limit); });
        limit = o;
        limit.control.cancelled = [] { return true; };
        rejects([&] { compose_binary_deltas(ab, bc, limit); });
        const std::vector<std::uint8_t> literal = {1, 2, 3};
        const auto insert = make_binary_delta({}, literal, o);
        BinaryDelta repeat = insert;
        repeat.base_bytes = 3;
        repeat.base_sha256 = insert.target_sha256;
        repeat.target_bytes = 6;
        repeat.target_sha256 = sha256_bytes({1, 2, 3, 1, 2, 3});
        repeat.operations = {{BinaryDeltaKind::copy, 0, 3, {}}, {BinaryDeltaKind::copy, 0, 3, {}}};
        limit = o;
        limit.max_literal_bytes = 3;
        rejects([&] { compose_binary_deltas(insert, repeat, limit); });
        check(apply_binary_delta({}, compose_binary_deltas(insert, repeat, o), o) ==
              std::vector<std::uint8_t>({1, 2, 3, 1, 2, 3}));
        const auto remove = make_binary_delta(literal, {}, o);
        check(apply_binary_delta({}, compose_binary_deltas(insert, remove, o), o).empty());
        check(apply_binary_delta(literal, compose_binary_deltas(remove, insert, o), o) == literal);
        std::cout << checks << " composition checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
