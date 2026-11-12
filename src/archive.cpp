#include "archive.hpp"
#include "sha256.hpp"
#include "string_value.hpp"
#include <cyuediff/string_encoding.hpp>
#include "descriptor.hpp"
#include <iomanip>
#include <sstream>
namespace cyuediff {
Error::Error(std::string c, std::string m, std::uint64_t o)
    : std::runtime_error(std::move(m)), code(std::move(c)), offset(o) {}
namespace detail {
namespace {
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
Archive::Archive(const fs::path &path, const ReadOptions &options) : Archive(file_source(path), options) {}
Archive::Archive(ByteSourcePtr source, const ReadOptions &options)
    : source_(std::move(source)), options_(options) {
    if (!source_)
        throw Error("byte-source", "Missing byte source");
    session_ = source_->session();
    if (!session_)
        throw Error("byte-source", "Byte source returned no session");
    end_ = source_->size();
    read_limit_ = end_;
    seek(0);
}
void Archive::fetch(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) {
    try {
        session_->read(offset, destination, bytes);
    } catch (const Error &) {
        throw;
    } catch (const std::exception &e) {
        throw Error("io", std::string("Read failed: ") + e.what(), offset);
    }
}
void Archive::seek(std::uint64_t p) {
    options_.control.check();
    if (p > read_limit_)
        throw Error("bounds", "Offset outside file", p);
    position_ = p;
}
void Archive::skip(std::uint64_t n) {
    if (n > read_limit_ - position_)
        throw Error("bounds", "Truncated data", position_);
    seek(position_ + n);
}
void Archive::read(void *destination, std::size_t bytes) {
    if (bytes > read_limit_ - position_)
        throw Error("bounds", "Truncated data", position_);
    options_.control.check();
    auto out = static_cast<std::uint8_t *>(destination);
    while (bytes) {
        std::size_t chunk = 0;
        if (position_ >= window_start_ && position_ - window_start_ < window_size_) {
            const auto at = static_cast<std::size_t>(position_ - window_start_);
            chunk = std::min(bytes, window_size_ - at);
            std::memcpy(out, window_.data() + at, chunk);
        } else if (bytes >= window_bytes) {
            // Large reads bypass the window, in bounded chunks for progress and cancellation.
            chunk = std::min<std::size_t>(bytes, 1024 * 1024);
            fetch(position_, out, chunk);
        } else {
            window_.resize(window_bytes);
            window_size_ = static_cast<std::size_t>(std::min<std::uint64_t>(window_bytes, end_ - position_));
            window_start_ = position_;
            try {
                fetch(window_start_, window_.data(), window_size_);
            } catch (...) {
                window_size_ = 0;
                throw;
            }
            continue;
        }
        out += chunk;
        bytes -= chunk;
        position_ += chunk;
        progress_bytes_ += chunk;
        if (progress_bytes_ >= 1024 * 1024) {
            progress_bytes_ = 0;
            options_.control.checkpoint("Reading file", position_, end_);
        }
    }
}
void Archive::read_at(std::uint64_t offset, void *destination, std::size_t bytes) {
    seek(offset);
    if (bytes > read_limit_ - position_)
        throw Error("bounds", "Truncated data", position_);
    auto out = static_cast<std::uint8_t *>(destination);
    while (bytes) {
        const auto chunk = std::min<std::size_t>(bytes, 1024 * 1024);
        fetch(position_, out, chunk);
        out += chunk;
        bytes -= chunk;
        position_ += chunk;
        progress_bytes_ += chunk;
        if (progress_bytes_ >= 1024 * 1024) {
            progress_bytes_ = 0;
            options_.control.checkpoint("Reading file", position_, end_);
        }
    }
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
        if (options_.narrow_string_encoding == NarrowStringEncoding::unspecified) {
            // Without an explicit character set, narrow strings must be ASCII (as in the schema and
            // layout readers): raw high bytes would otherwise become invalid UTF-8 in every report.
            validate_byte_string(s, true, options_.control, "string-encoding", tell() - s.size() - 1);
            return s;
        }
        return decode_narrow_string(s, options_.narrow_string_encoding, options_.max_string_bytes,
                                    options_.control);
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
std::string Archive::byte_string(bool ascii) {
    const auto n = get<std::int32_t>();
    if (n < 0)
        throw Error("string-length", "Negative byte string length", tell() - 4);
    if (static_cast<std::uint32_t>(n) > options_.max_string_bytes)
        throw Error("limit", "Byte string exceeds configured limit", tell() - 4);
    if (static_cast<std::uint32_t>(n) > read_limit_ - position_)
        throw Error("bounds", "Truncated byte string", tell());
    std::string value(static_cast<std::size_t>(n), '\0');
    const auto offset = tell();
    read(value.data(), value.size());
    if (ascii && options_.narrow_string_encoding != NarrowStringEncoding::unspecified)
        return decode_narrow_string(value, options_.narrow_string_encoding, options_.max_string_bytes, options_.control);
    validate_byte_string(value, ascii, options_.control, "string-encoding", offset);
    return value;
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
std::string hash_range(const std::vector<std::uint8_t> &bytes, std::uint64_t offset, std::uint64_t size,
                       const OperationControl &control) {
    if (offset > bytes.size() || size > bytes.size() - offset)
        throw Error("bounds", "Hash range exceeds its source", offset);
    Sha256 hash;
    control.check();
    for (std::uint64_t at = 0; at < size;) {
        auto count = static_cast<std::size_t>(std::min<std::uint64_t>(size - at, 1024 * 1024));
        hash.update(bytes.data() + static_cast<std::size_t>(offset + at), count);
        at += count;
        control.checkpoint("Hashing data", at, size);
    }
    return hash.finish();
}
std::string hash_bytes(const std::vector<std::uint8_t> &bytes, const OperationControl &control) {
    return hash_range(bytes, 0, bytes.size(), control);
}
} // namespace detail
std::string sha256_bytes(const std::vector<std::uint8_t> &bytes, const OperationControl &control) {
    return detail::hash_bytes(bytes, control);
}
std::string sha256_file(const fs::path &path, const OperationControl &control) {
    ReadOptions o;
    o.control = control;
    detail::Archive a(path, o);
    return a.hash(0, a.size());
}
} // namespace cyuediff
