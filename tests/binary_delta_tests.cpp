#include <cyuediff/binary_delta.hpp>
#include <iostream>
#include <random>
using namespace cyuediff;
namespace {
std::vector<std::uint8_t> reconstruct(const std::vector<std::uint8_t> &base, const BinaryDelta &d) {
    std::vector<std::uint8_t> result;
    for (const auto &op : d.operations) {
        if (op.kind == BinaryDeltaKind::copy)
            result.insert(result.end(), base.begin() + op.offset, base.begin() + op.offset + op.bytes);
        else
            result.insert(result.end(), op.data.begin(), op.data.end());
    }
    return result;
}
} // namespace
int main() {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Binary delta check failed");
    };
    auto rejects = [&](auto action) {
        bool failed = false;
        try {
            action();
        } catch (const Error &) {
            failed = true;
        }
        check(failed);
    };
    try {
        BinaryDeltaOptions o;
        o.block_size = 32;
        std::mt19937 rng(417);
        std::vector<std::uint8_t> base(4099);
        for (auto &b : base)
            b = std::uint8_t(rng());
        auto verify = [&](const std::vector<std::uint8_t> &a, const std::vector<std::uint8_t> &b) {
            const auto d = make_binary_delta(a, b, o);
            check(reconstruct(a, d) == b);
            check(d.base_sha256 == sha256_bytes(a));
            check(d.target_sha256 == sha256_bytes(b));
            const auto stats = d.summary(o);
            check(stats["copiedBytes"].get<std::uint64_t>() + stats["literalBytes"].get<std::uint64_t>() ==
                  b.size());
            return d;
        };
        check(verify({}, {}).operations.empty());
        check(verify({1, 2, 3}, {1, 2, 3}).operations.front().kind == BinaryDeltaKind::copy);
        verify({}, base);
        verify(base, {});
        verify(base, base);
        auto target = base;
        target.insert(target.begin() + 19, {1, 2, 3, 4, 5});
        check(verify(base, target).summary(o)["copiedBytes"].get<std::size_t>() > 4000);
        target = base;
        target.erase(target.begin() + 17, target.begin() + 94);
        check(verify(base, target).summary(o)["literalBytes"].get<std::size_t>() < 100);
        target = base;
        target[201] ^= 1;
        verify(base, target);
        target.insert(target.end(), base.begin(), base.end());
        verify(base, target);
        verify(std::vector<std::uint8_t>(4096, 0), std::vector<std::uint8_t>(8096, 0));
        // Independent patch simulation, covering shifts, replacements, repeated blocks and deletions.
        for (int pass = 0; pass < 250; ++pass) {
            target = base;
            for (int edit = 0; edit < 8; ++edit) {
                const auto at = rng() % (target.size() + 1);
                if ((rng() & 1) || at == target.size())
                    target.insert(target.begin() + at, std::uint8_t(rng()));
                else
                    target.erase(target.begin() + at,
                                 target.begin() + std::min(target.size(), at + rng() % 100));
            }
            verify(base, target);
        }
        // (257 - 1)^8 == 0 modulo 2^64: distinct windows with the same rolling hash.
        std::vector<std::uint8_t> collision_base(16, 128), collision_target = collision_base;
        const int coefficients[] = {1, -8, 28, -56, 70, -56, 28, -8, 1};
        for (std::size_t i = 0; i < 9; ++i)
            collision_target[i] += coefficients[i];
        auto collision_options = o;
        collision_options.block_size = 16;
        auto collision = make_binary_delta(collision_base, collision_target, collision_options);
        check(reconstruct(collision_base, collision) == collision_target);
        check(collision.summary(collision_options)["copiedBytes"] == 0);
        collision_base.resize(16 * 1024, 128);
        collision_options.max_match_bytes = 128;
        rejects([&] { make_binary_delta(collision_base, collision_target, collision_options); });
        auto d = make_binary_delta(base, base, o);
        d.operations.front().offset = 1;
        rejects([&] { d.summary(o); });
        auto limit = o;
        limit.max_input_bytes = 1;
        rejects([&] { make_binary_delta(base, base, limit); });
        limit = o;
        limit.max_blocks = 1;
        rejects([&] { make_binary_delta(base, {}, limit); });
        limit = o;
        limit.max_literal_bytes = 0;
        rejects([&] { make_binary_delta({}, base, limit); });
        limit = o;
        limit.max_match_bytes = 1;
        target = base;
        target.push_back(0);
        rejects([&] { make_binary_delta(base, target, limit); });
        limit = o;
        limit.max_operations = 1;
        rejects([&] { make_binary_delta(base, target, limit); });
        limit = o;
        limit.block_size = 0;
        rejects([&] { make_binary_delta({}, {}, limit); });
        limit = o;
        limit.control.cancelled = [] { return true; };
        rejects([&] { make_binary_delta(base, base, limit); });
        int progress = 0;
        limit = o;
        limit.control.progress = [&](const OperationProgress &) { ++progress; };
        make_binary_delta(base, target, limit);
        check(progress > 0);
        std::cout << checks << " binary delta planning checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
