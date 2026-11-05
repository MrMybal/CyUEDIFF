#include <cyuediff/binary_delta.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
using namespace cyuediff;
int main() {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Source delta check failed");
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
        std::uint64_t reads = 0, bytes = 0;
        std::size_t largest = 0;
        auto source = [&](const std::vector<std::uint8_t> &v) {
            return BinaryDeltaSource{v.size(),
                                     [&, p = &v](std::uint64_t at, std::uint8_t *out, std::size_t n) {
                                         if (at > p->size() || n > p->size() - at)
                                             throw std::runtime_error("Callback range escaped");
                                         ++reads;
                                         bytes += n;
                                         largest = std::max(largest, n);
                                         std::memcpy(out, p->data() + static_cast<std::size_t>(at), n);
                                         return n;
                                     }};
        };
        BinaryDeltaOptions o;
        o.block_size = 16;
        std::mt19937 rng(168);
        for (unsigned i = 0; i < 150; ++i) {
            std::vector<std::uint8_t> a(256 + rng() % 2048);
            for (auto &b : a)
                b = std::uint8_t(rng());
            auto b = a;
            b.insert(b.begin() + rng() % b.size(), {0, 255, 17, 0, 92});
            if (i % 2)
                std::reverse(b.begin(), b.end());
            const auto expected = make_binary_delta(a, b, o);
            const auto plan = make_binary_delta_sources(source(a), source(b), o);
            check(plan.to_json(o) == expected.to_json(o));
            check(apply_binary_delta(a, plan, o) == b);
        }
        std::vector<std::uint8_t> a(8192, 37);
        reads = bytes = largest = 0;
        auto limit = o;
        limit.max_source_read_bytes = 4 * a.size();
        const auto d = make_binary_delta_sources(source(a), source(a), limit);
        check(bytes == 4 * a.size() && reads == 4 && apply_binary_delta(a, d, o) == a);
        --limit.max_source_read_bytes;
        rejects([&] { make_binary_delta_sources(source(a), source(a), limit); });
        limit = o;
        limit.max_source_read_bytes = 0;
        rejects([&] { make_binary_delta_sources({}, {}, limit); });
        check(make_binary_delta_sources({}, {}, o).operations.empty());
        check(apply_binary_delta({}, make_binary_delta_sources({}, source(a), o), o) == a);
        check(make_binary_delta_sources(source(a), {}, o).operations.empty());
        rejects([&] { make_binary_delta_sources({10, {}}, {}, o); });
        reads = 0;
        limit = o;
        limit.max_input_bytes = 1;
        rejects([&] { make_binary_delta_sources(source(a), {}, limit); });
        check(reads == 0);
        auto short_read = source(a);
        short_read.read = [](std::uint64_t, std::uint8_t *, std::size_t n) { return n - 1; };
        rejects([&] { make_binary_delta_sources(short_read, source(a), o); });
        short_read.read = [](std::uint64_t, std::uint8_t *, std::size_t n) { return n + 1; };
        rejects([&] { make_binary_delta_sources(short_read, source(a), o); });
        bool propagated = false;
        short_read.read = [](std::uint64_t, std::uint8_t *, std::size_t) -> std::size_t {
            throw std::runtime_error("host-read");
        };
        try {
            make_binary_delta_sources(short_read, source(a), o);
        } catch (const std::runtime_error &e) {
            propagated = std::string(e.what()) == "host-read";
        }
        check(propagated);
        unsigned calls = 0;
        auto unstable = source(a);
        unstable.read = [&](std::uint64_t at, std::uint8_t *out, std::size_t n) {
            std::memcpy(out, a.data() + at, n);
            if (++calls == 2)
                out[0] ^= 1;
            return n;
        };
        rejects([&] { make_binary_delta_sources(unstable, source(a), o); });
        limit = o;
        limit.control.cancelled = [] { return true; };
        reads = 0;
        rejects([&] { make_binary_delta_sources(source(a), source(a), limit); });
        check(reads == 0);
        unsigned progress = 0;
        limit = o;
        limit.control.progress = [&](const OperationProgress &) { ++progress; };
        make_binary_delta_sources(source(a), source(a), limit);
        check(progress > 0);
        // Rolling endpoints one block apart require two retained source windows.
        a.resize(3 * 1024 * 1024);
        for (auto &b : a)
            b = std::uint8_t(rng());
        auto b = a;
        b.insert(b.begin() + 1, {255});
        reads = bytes = largest = 0;
        limit = o;
        limit.block_size = 1024 * 1024;
        limit.max_source_read_bytes = 64 * 1024 * 1024;
        const auto large = make_binary_delta_sources(source(a), source(b), limit);
        check(large.to_json(limit) == make_binary_delta(a, b, limit).to_json(limit));
        check(apply_binary_delta(a, large, limit) == b);
        check(largest <= 1024 * 1024 && reads < 64 && bytes <= limit.max_source_read_bytes);
        std::cout << checks << " callback source checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
