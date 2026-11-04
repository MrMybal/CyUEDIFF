#pragma once
#include <cyuediff/core.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <type_traits>
namespace cyuediff::detail {
class Archive {
    std::ifstream stream_;
    std::uint64_t position_ = 0, end_ = 0, read_limit_ = 0, progress_bytes_ = 0;
    const ReadOptions &options_;

  public:
    Archive(const fs::path &path, const ReadOptions &options);
    // Restrict native decoders to their export, including reads inside FString.
    class ReadLimit {
        Archive &archive_;
        std::uint64_t previous_;

      public:
        ReadLimit(Archive &archive, std::uint64_t end) : archive_(archive), previous_(archive.read_limit_) {
            if (end > previous_ || end < archive.tell())
                throw Error("bounds", "Invalid decoder read window", end);
            archive_.read_limit_ = end;
        }
        ~ReadLimit() {
            archive_.read_limit_ = previous_;
        }
        ReadLimit(const ReadLimit &) = delete;
        ReadLimit &operator=(const ReadLimit &) = delete;
    };
    std::uint64_t tell() const {
        return position_;
    }
    std::uint64_t size() const {
        return end_;
    }
    void seek(std::uint64_t position);
    void skip(std::uint64_t bytes);
    void read(void *destination, std::size_t bytes);
    template <class T> T get() {
        static_assert(std::is_arithmetic_v<T>);
        std::array<unsigned char, sizeof(T)> bytes{};
        read(bytes.data(), bytes.size());
        const std::uint16_t endian = 1;
        if (*reinterpret_cast<const unsigned char *>(&endian) != 1)
            std::reverse(bytes.begin(), bytes.end());
        T result;
        std::memcpy(&result, bytes.data(), sizeof(T));
        return result;
    }
    std::uint32_t count(std::uint32_t limit = 0);
    std::string string();
    std::string byte_string(bool ascii);
    std::string guid();
    std::string hash(std::uint64_t offset, std::uint64_t bytes);
};
std::string category(const std::string &class_name);
Json load_json(const fs::path &path);
std::string hash_range(const std::vector<std::uint8_t> &, std::uint64_t offset, std::uint64_t size,
                       const OperationControl &control);
} // namespace cyuediff::detail
