#include <cyuediff/binary_delta.hpp>
#include <algorithm>
#include <iostream>
using namespace cyuediff;
int main() {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Compact sink check failed");
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
        std::vector<std::uint8_t> payload(3 * 1024 * 1024 + 79);
        for (std::size_t i = 0; i < payload.size(); ++i)
            payload[i] = std::uint8_t(i);
        const auto d = make_binary_delta({}, payload, o);
        std::vector<std::uint8_t> bytes;
        std::size_t largest = 0, calls = 0;
        const BinaryDeltaSink sink = [&](const std::uint8_t *p, std::size_t n) {
            ++calls;
            largest = std::max(largest, n);
            bytes.insert(bytes.end(), p, p + n);
        };
        d.write_binary(sink, o);
        check(calls == 6 && largest == 1024 * 1024);
        check(d.binary_size(o) == 112 + 9 + payload.size() && bytes.size() == d.binary_size(o));
        check(bytes == d.to_binary(o));
        check(apply_binary_delta({}, BinaryDelta::from_binary(bytes, o), o) == payload);
        calls = 0;
        auto bad = d;
        bad.operations[0].bytes = 0;
        rejects([&] { bad.write_binary(sink, o); });
        check(calls == 0);
        rejects([&] { bad.binary_size(o); });
        auto limit = o;
        limit.max_manifest_bytes = d.binary_size(o) - 1;
        rejects([&] { d.write_binary(sink, limit); });
        check(calls == 0);
        rejects([&] { d.binary_size(limit); });
        rejects([&] { d.write_binary({}, o); });
        check(calls == 0);
        limit = o;
        limit.control.cancelled = [] { return true; };
        rejects([&] { d.write_binary(sink, limit); });
        check(calls == 0);
        bool cancelled = false;
        limit = o;
        limit.control.cancelled = [&] { return cancelled; };
        rejects([&] {
            d.write_binary(
                [&](const std::uint8_t *, std::size_t) {
                    ++calls;
                    cancelled = true;
                },
                limit);
        });
        check(calls == 1);
        bool propagated = false;
        try {
            d.write_binary([](const std::uint8_t *, std::size_t) { throw std::runtime_error("sink-host"); },
                           o);
        } catch (const std::runtime_error &e) {
            propagated = std::string(e.what()) == "sink-host";
        }
        check(propagated);
        const auto empty = make_binary_delta({}, {}, o);
        bytes.clear();
        empty.write_binary(sink, o);
        check(bytes.size() == 112 && empty.binary_size(o) == 112);
        limit = o;
        limit.max_manifest_bytes = 112;
        check(empty.binary_size(limit) == 112);
        std::vector<std::uint8_t> a(4096, 9), b = a;
        b.insert(b.begin() + 1024, {255, 0, 128});
        const auto mixed = make_binary_delta(a, b, o);
        bytes.clear();
        mixed.write_binary(sink, o);
        check(bytes == mixed.to_binary(o) &&
              apply_binary_delta(a, BinaryDelta::from_binary(bytes, o), o) == b);
        check(sha256_bytes(bytes) == sha256_bytes(mixed.to_binary(o)));
        std::cout << checks << " compact sink checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
