#include <cyuediff/core.hpp>
#include <algorithm>
#include <fstream>
namespace cyuediff {
namespace {
void require_range(const ByteSource &source, std::uint64_t offset, std::size_t bytes) {
    if (offset > source.size() || bytes > source.size() - offset)
        throw Error("bounds", "Read outside byte source", offset);
}
class ForwardingSession final : public ByteSource::Session {
    const ByteSource &source_;

  public:
    explicit ForwardingSession(const ByteSource &source) : source_(source) {}
    void read(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) override {
        source_.read(offset, destination, bytes);
    }
};
std::ifstream open_file(const fs::path &path, std::uint64_t expected) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw Error("io", "Cannot open " + path.u8string());
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0)
        throw Error("io", "Cannot determine file size");
    if (static_cast<std::uint64_t>(end) != expected)
        throw Error("source-changed", "File size changed after it was opened: " + path.u8string());
    return stream;
}
void read_file(std::ifstream &stream, std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) {
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    while (bytes) {
        const auto chunk = std::min<std::size_t>(bytes, 1u << 30);
        stream.read(reinterpret_cast<char *>(destination), static_cast<std::streamsize>(chunk));
        if (!stream || static_cast<std::size_t>(stream.gcount()) != chunk)
            throw Error("io", "Read failed", offset);
        destination += chunk;
        offset += chunk;
        bytes -= chunk;
    }
}
class FileSource final : public ByteSource {
    fs::path path_;
    std::uint64_t size_ = 0;

    class FileSession final : public Session {
        const FileSource &owner_;
        std::ifstream stream_;

      public:
        explicit FileSession(const FileSource &owner) : owner_(owner), stream_(open_file(owner.path_, owner.size_)) {}
        void read(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) override {
            require_range(owner_, offset, bytes);
            read_file(stream_, offset, destination, bytes);
        }
    };

  public:
    explicit FileSource(const fs::path &path) : path_(path) {
        std::error_code error;
        const auto regular = fs::is_regular_file(path_, error);
        const auto size = regular && !error ? fs::file_size(path_, error) : 0;
        if (!regular || error)
            throw Error("io", "Cannot open " + path_.u8string());
        size_ = size;
    }
    std::uint64_t size() const override {
        return size_;
    }
    void read(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) const override {
        require_range(*this, offset, bytes);
        auto stream = open_file(path_, size_);
        read_file(stream, offset, destination, bytes);
    }
    std::string name() const override {
        return path_.u8string();
    }
    std::unique_ptr<Session> session() const override {
        return std::make_unique<FileSession>(*this);
    }
};
class MemorySource final : public ByteSource {
    std::shared_ptr<const void> owner_;
    const std::uint8_t *data_;
    std::size_t size_;
    std::string name_;

  public:
    MemorySource(std::shared_ptr<const void> owner, const std::uint8_t *data, std::size_t size, std::string name)
        : owner_(std::move(owner)), data_(data), size_(size), name_(std::move(name)) {
        if (!data_ && size_)
            throw Error("byte-source", "Memory source has no data");
    }
    std::uint64_t size() const override {
        return size_;
    }
    void read(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) const override {
        require_range(*this, offset, bytes);
        if (bytes)
            std::copy_n(data_ + static_cast<std::size_t>(offset), bytes, destination);
    }
    std::string name() const override {
        return name_;
    }
};
class CallbackSource final : public ByteSource {
    std::uint64_t size_;
    ByteReadCallback read_;
    std::string name_;

  public:
    CallbackSource(std::uint64_t size, ByteReadCallback read, std::string name)
        : size_(size), read_(std::move(read)), name_(std::move(name)) {
        if (!read_)
            throw Error("byte-source", "Callback source requires a read function");
    }
    std::uint64_t size() const override {
        return size_;
    }
    void read(std::uint64_t offset, std::uint8_t *destination, std::size_t bytes) const override {
        require_range(*this, offset, bytes);
        read_(offset, destination, bytes);
    }
    std::string name() const override {
        return name_;
    }
};
} // namespace
std::unique_ptr<ByteSource::Session> ByteSource::session() const {
    return std::make_unique<ForwardingSession>(*this);
}
ByteSourcePtr file_source(const fs::path &path) {
    return std::make_shared<FileSource>(path);
}
ByteSourcePtr memory_source(std::vector<std::uint8_t> bytes, std::string name) {
    auto owned = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    const auto *data = owned->data();
    const auto size = owned->size();
    return std::make_shared<MemorySource>(std::move(owned), data, size, std::move(name));
}
ByteSourcePtr memory_source(std::shared_ptr<const void> owner, const std::uint8_t *data, std::size_t size,
                            std::string name) {
    return std::make_shared<MemorySource>(std::move(owner), data, size, std::move(name));
}
ByteSourcePtr callback_source(std::uint64_t size, ByteReadCallback read, std::string name) {
    return std::make_shared<CallbackSource>(size, std::move(read), std::move(name));
}
const std::array<const char *, 6> &PackageSources::companion_extensions() {
    static const std::array<const char *, 6> extensions = {".uexp",    ".ubulk",    ".uptnl",
                                                           ".m.ubulk", ".o.ubulk", ".upayload"};
    return extensions;
}
PackageSources PackageSources::files(const fs::path &asset) {
    PackageSources result;
    result.package = file_source(asset);
    result.name = asset;
    for (const auto *extension : companion_extensions()) {
        auto companion = asset;
        companion.replace_extension(extension);
        std::error_code error;
        if (fs::is_regular_file(companion, error) && !error)
            result.companions[extension] = file_source(companion);
    }
    return result;
}
ByteSourcePtr PackageSources::companion(const std::string &extension) const {
    const auto it = companions.find(extension);
    return it == companions.end() ? nullptr : it->second;
}
void PackageSources::validate() const {
    if (!package)
        throw Error("byte-source", "Package sources require the package bytes");
    const auto &known = companion_extensions();
    for (const auto &entry : companions) {
        if (std::none_of(known.begin(), known.end(), [&](const char *e) { return entry.first == e; }))
            throw Error("byte-source", "Unknown package companion extension: " + entry.first);
        if (!entry.second)
            throw Error("byte-source", "Package companion source is null: " + entry.first);
    }
}
} // namespace cyuediff
