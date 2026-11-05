#include "binary_delta_internal.hpp"
#include <algorithm>
namespace cyuediff {
namespace {
constexpr std::uint8_t magic[] = {'C', 'Y', 'U', 'E', 'D', 'L', 'T', 0};
constexpr std::uint64_t header_bytes = 112;
void require(bool good, const char *message) {
    if (!good)
        throw Error("binary-delta", message);
}
void integer(std::vector<std::uint8_t> &out, std::uint64_t n, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        out.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
}
unsigned nibble(char c) {
    return c <= '9' ? unsigned(c - '0') : unsigned(c - 'a' + 10);
}
void digest(std::vector<std::uint8_t> &out, const std::string &s) {
    for (std::size_t i = 0; i < s.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>((nibble(s[i]) << 4) | nibble(s[i + 1])));
}
class Reader {
    const std::vector<std::uint8_t> &bytes_;
    std::size_t at_ = 0;

  public:
    explicit Reader(const std::vector<std::uint8_t> &b) : bytes_(b) {}
    std::size_t left() const {
        return bytes_.size() - at_;
    }
    std::uint64_t integer(unsigned n) {
        require(n <= left(), "Truncated compact delta");
        std::uint64_t value = 0;
        for (unsigned i = 0; i < n; ++i)
            value |= std::uint64_t(bytes_[at_++]) << (8 * i);
        return value;
    }
    std::string digest() {
        std::string value;
        constexpr char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < 32; ++i) {
            const auto b = integer(1);
            value += digits[b >> 4];
            value += digits[b & 15];
        }
        return value;
    }
    void literal(std::vector<std::uint8_t> &out, std::uint64_t n, const OperationControl &control) {
        require(n <= left() && n <= out.max_size(), "Truncated or excessive compact literal");
        out.reserve(static_cast<std::size_t>(n));
        while (n) {
            control.check();
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(n, 1024 * 1024));
            out.insert(out.end(), bytes_.begin() + at_, bytes_.begin() + at_ + count);
            at_ += count;
            n -= count;
        }
    }
};
} // namespace
std::vector<std::uint8_t> BinaryDelta::to_binary(const BinaryDeltaOptions &o) const {
    detail::binary_delta_validate(*this, o);
    std::uint64_t size = 0;
    auto charge = [&](std::uint64_t n) {
        require(n <= o.max_manifest_bytes - size, "Compact manifest byte budget exceeded");
        size += n;
    };
    charge(header_bytes);
    for (const auto &op : operations) {
        o.control.check();
        charge(op.kind == BinaryDeltaKind::copy ? 17 : 9);
        if (op.kind == BinaryDeltaKind::literal)
            charge(op.bytes);
    }
    std::vector<std::uint8_t> out;
    require(size <= out.max_size(), "Compact manifest allocation exceeds address space");
    out.reserve(static_cast<std::size_t>(size));
    out.insert(out.end(), std::begin(magic), std::end(magic));
    integer(out, 1, 4);
    integer(out, 0, 4);
    integer(out, block_size, 4);
    integer(out, 0, 4);
    integer(out, base_bytes, 8);
    integer(out, target_bytes, 8);
    integer(out, operations.size(), 8);
    digest(out, base_sha256);
    digest(out, target_sha256);
    for (const auto &op : operations) {
        o.control.check();
        integer(out, op.kind == BinaryDeltaKind::copy ? 0 : 1, 1);
        if (op.kind == BinaryDeltaKind::copy)
            integer(out, op.offset, 8);
        integer(out, op.bytes, 8);
        if (op.kind == BinaryDeltaKind::literal)
            for (std::size_t at = 0; at < op.data.size();) {
                o.control.check();
                const auto n = std::min<std::size_t>(op.data.size() - at, 1024 * 1024);
                out.insert(out.end(), op.data.begin() + at, op.data.begin() + at + n);
                at += n;
            }
    }
    return out;
}
BinaryDelta BinaryDelta::from_binary(const std::vector<std::uint8_t> &bytes, const BinaryDeltaOptions &o) {
    detail::binary_delta_options(o);
    require(bytes.size() >= header_bytes && bytes.size() <= o.max_manifest_bytes,
            "Truncated or excessive compact manifest");
    Reader in(bytes);
    for (const auto b : magic)
        require(in.integer(1) == b, "Invalid compact delta signature");
    require(in.integer(4) == 1, "Unsupported compact delta version");
    require(in.integer(4) == 0, "Unsupported compact delta flags");
    BinaryDelta d;
    d.block_size = static_cast<std::uint32_t>(in.integer(4));
    require(in.integer(4) == 0, "Nonzero compact delta reserved field");
    d.base_bytes = in.integer(8);
    d.target_bytes = in.integer(8);
    const auto count = in.integer(8);
    require(d.block_size >= 16 && d.block_size <= 1024 * 1024 && d.base_bytes <= o.max_input_bytes &&
                d.target_bytes <= o.max_input_bytes,
            "Invalid compact delta identity or limits");
    d.base_sha256 = in.digest();
    d.target_sha256 = in.digest();
    require(count <= o.max_operations && count <= in.left() / 9, "Compact operation budget exceeded");
    std::uint64_t literals = 0, total = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        o.control.check();
        BinaryDeltaOperation op;
        const auto kind = in.integer(1);
        require(kind <= 1, "Unknown compact delta operation");
        op.kind = kind ? BinaryDeltaKind::literal : BinaryDeltaKind::copy;
        if (!kind)
            op.offset = in.integer(8);
        op.bytes = in.integer(8);
        require(op.bytes && op.bytes <= d.target_bytes - total, "Invalid compact target extent");
        total += op.bytes;
        if (!kind)
            require(op.offset <= d.base_bytes && op.bytes <= d.base_bytes - op.offset,
                    "Compact copy exceeds original base");
        else {
            require(op.bytes <= o.max_literal_bytes - literals, "Compact literal budget exceeded");
            literals += op.bytes;
            in.literal(op.data, op.bytes, o.control);
        }
        d.operations.push_back(std::move(op));
    }
    require(!in.left(), "Trailing compact delta bytes");
    d.summary(o);
    return d;
}
} // namespace cyuediff
