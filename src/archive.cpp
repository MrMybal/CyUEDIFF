#include "archive.hpp"
#include "descriptor.hpp"
#include <iomanip>
#include <sstream>
namespace cyuediff {
Error::Error(std::string c, std::string m, std::uint64_t o)
    : std::runtime_error(std::move(m)), code(std::move(c)), offset(o) {}
namespace detail {
namespace {
constexpr std::uint32_t constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
std::uint32_t rotate(std::uint32_t v, unsigned n) {
    return (v >> n) | (v << (32 - n));
}
class Sha256 {
    std::uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buffer_[64]{};
    std::size_t used_ = 0;
    std::uint64_t bytes_ = 0;
    void block(const unsigned char *p) {
        std::uint32_t w[64]{};
        for (unsigned i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
                   (std::uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
        for (unsigned i = 16; i < 64; ++i) {
            auto a = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            auto b = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + a + w[i - 7] + b;
        }
        auto a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (unsigned i = 0; i < 64; ++i) {
            auto t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) +
                      constants[i] + w[i];
            auto t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += h;
    }

  public:
    void update(const unsigned char *p, std::size_t n) {
        bytes_ += n;
        while (n) {
            auto take = std::min(n, 64 - used_);
            std::memcpy(buffer_ + used_, p, take);
            used_ += take;
            p += take;
            n -= take;
            if (used_ == 64) {
                block(buffer_);
                used_ = 0;
            }
        }
    }
    std::string finish() {
        auto bits = bytes_ * 8;
        unsigned char one = 0x80, zero = 0;
        update(&one, 1);
        while (used_ != 56)
            update(&zero, 1);
        unsigned char length[8];
        for (int i = 0; i < 8; ++i)
            length[7 - i] = static_cast<unsigned char>(bits >> (i * 8));
        update(length, 8);
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (auto v : h_)
            out << std::setw(8) << v;
        return out.str();
    }
};
void utf8(std::string &out, std::uint32_t c) {
    if (c < 0x80)
        out += char(c);
    else if (c < 0x800) {
        out += char(0xc0 | (c >> 6));
        out += char(0x80 | (c & 63));
    } else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 63));
        out += char(0x80 | (c & 63));
    } else {
        out += char(0xf0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 63));
        out += char(0x80 | ((c >> 6) & 63));
        out += char(0x80 | (c & 63));
    }
}
} // namespace
Archive::Archive(const fs::path &path, const ReadOptions &options)
    : stream_(path, std::ios::binary), options_(options) {
    if (!stream_)
        throw Error("io", "Cannot open " + path.u8string());
    stream_.seekg(0, std::ios::end);
    auto n = stream_.tellg();
    if (n < 0)
        throw Error("io", "Cannot determine file size");
    end_ = static_cast<std::uint64_t>(n);
    read_limit_ = end_;
    seek(0);
}
void Archive::seek(std::uint64_t p) {
    if (p > read_limit_)
        throw Error("bounds", "Offset outside file", p);
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(p));
    position_ = p;
    if (!stream_)
        throw Error("io", "Seek failed", p);
}
void Archive::skip(std::uint64_t n) {
    if (n > read_limit_ - position_)
        throw Error("bounds", "Truncated data", position_);
    seek(position_ + n);
}
void Archive::read(void *destination, std::size_t bytes) {
    if (bytes > read_limit_ - position_)
        throw Error("bounds", "Truncated data", position_);
    stream_.read(static_cast<char *>(destination), static_cast<std::streamsize>(bytes));
    if (!stream_)
        throw Error("io", "Read failed", position_);
    position_ += bytes;
}
std::uint32_t Archive::count(std::uint32_t limit) {
    auto n = get<std::int32_t>();
    if (n < 0 || static_cast<std::uint32_t>(n) > (limit ? limit : options_.max_table_entries))
        throw Error("limit", "Invalid or excessive count", tell() - 4);
    return static_cast<std::uint32_t>(n);
}
std::string Archive::string() {
    auto n = get<std::int32_t>();
    if (n == 0)
        return {};
    auto chars = n < 0 ? -std::int64_t(n) : std::int64_t(n);
    if (chars * (n < 0 ? 2 : 1) > options_.max_string_bytes)
        throw Error("limit", "String exceeds configured limit", tell() - 4);
    if (n > 0) {
        std::string s(static_cast<std::size_t>(chars), '\0');
        read(s.data(), s.size());
        if (s.back() != 0)
            throw Error("format", "Unterminated FString", tell());
        s.pop_back();
        return s;
    }
    std::string s;
    for (std::int64_t i = 0; i < chars; ++i) {
        std::uint32_t c = get<std::uint16_t>();
        if (i == chars - 1) {
            if (c)
                throw Error("format", "Unterminated UTF-16 string", tell());
            break;
        }
        if (c >= 0xd800 && c <= 0xdbff) {
            if (i + 2 >= chars)
                throw Error("format", "Invalid UTF-16 surrogate", tell());
            auto low = get<std::uint16_t>();
            ++i;
            if (low < 0xdc00 || low > 0xdfff)
                throw Error("format", "Invalid UTF-16 surrogate", tell());
            c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
        } else if (c >= 0xdc00 && c <= 0xdfff)
            throw Error("format", "Invalid UTF-16 surrogate", tell());
        utf8(s, c);
    }
    return s;
}
std::string Archive::guid() {
    std::ostringstream s;
    s << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i)
        s << std::setw(8) << get<std::uint32_t>();
    return s.str();
}
std::string Archive::hash(std::uint64_t offset, std::uint64_t bytes) {
    auto saved = tell();
    seek(offset);
    if (bytes > size() - offset)
        throw Error("bounds", "Hash range outside file", offset);
    Sha256 hash;
    std::array<unsigned char, 65536> buffer{};
    while (bytes) {
        auto n = static_cast<std::size_t>(std::min<std::uint64_t>(bytes, buffer.size()));
        read(buffer.data(), n);
        hash.update(buffer.data(), n);
        bytes -= n;
    }
    seek(saved);
    return hash.finish();
}
Json load_json(const fs::path &p) {
    std::error_code error;
    auto size = fs::file_size(p, error);
    if (error)
        throw Error("io", "Cannot read " + p.u8string());
    if (size > 16 * 1024 * 1024)
        throw Error("limit", "Project descriptor exceeds 16 MiB");
    std::ifstream in(p, std::ios::binary);
    if (!in)
        throw Error("io", "Cannot open " + p.u8string());
    std::string source(static_cast<std::size_t>(size), '\0');
    in.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (!in || in.peek() != std::char_traits<char>::eof())
        throw Error("io", "Descriptor changed during read");
    try {
        return parse_descriptor(std::move(source));
    } catch (const Json::exception &e) {
        throw Error("json", e.what());
    }
}
std::string category(const std::string &c) {
    if (c.find("Blueprint") != std::string::npos || c.find("EdGraph") != std::string::npos ||
        c.find("K2Node") != std::string::npos)
        return "blueprint";
    if (c.find("Texture") != std::string::npos)
        return "texture";
    if (c.find("Mesh") != std::string::npos)
        return "mesh";
    if (c.find("Material") != std::string::npos)
        return "material";
    if (c.find("DataAsset") != std::string::npos || c.find("DataTable") != std::string::npos)
        return "data";
    return "object";
}
std::string hash_bytes(const std::vector<std::uint8_t> &bytes) {
    Sha256 hash;
    hash.update(bytes.data(), bytes.size());
    return hash.finish();
}
} // namespace detail
std::string sha256_bytes(const std::vector<std::uint8_t> &bytes) {
    return detail::hash_bytes(bytes);
}
std::string sha256_file(const fs::path &path) {
    ReadOptions o;
    detail::Archive a(path, o);
    return a.hash(0, a.size());
}
} // namespace cyuediff
