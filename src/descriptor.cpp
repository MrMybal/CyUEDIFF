#include "descriptor.hpp"
#include <cctype>
namespace cyuediff::detail {
Json parse_descriptor(std::string source) {
    // Mask comments without changing positions or interpreting their contents.
    bool quoted = false, escaped = false;
    for (std::size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
            continue;
        }
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (c != '/' || i + 1 == source.size())
            continue;
        if (source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n' && source[i] != '\r')
                source[i++] = ' ';
            if (i == source.size())
                break;
        } else if (source[i + 1] == '*') {
            source[i++] = ' ';
            source[i++] = ' ';
            bool closed = false;
            for (; i < source.size(); ++i) {
                if (source[i] == '*' && i + 1 < source.size() && source[i + 1] == '/') {
                    source[i] = source[i + 1] = ' ';
                    ++i;
                    closed = true;
                    break;
                }
                if (source[i] != '\n' && source[i] != '\r')
                    source[i] = ' ';
            }
            if (!closed)
                throw Error("json", "Unterminated descriptor comment");
        }
    }
    quoted = escaped = false;
    std::vector<char> containers;
    char previous = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"') {
                quoted = false;
                previous = '"';
            }
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)))
            continue;
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (c == '[' || c == '{') {
            if (containers.size() >= 128)
                throw Error("json-depth", "Descriptor nesting exceeds 128 levels");
            containers.push_back(c);
        } else if (c == ']' || c == '}') {
            if (!containers.empty())
                containers.pop_back();
        } else if (c == ',' && !containers.empty() && containers.back() == '[' && previous != '[' &&
                   previous != ',' && previous != ':') {
            auto next = i + 1;
            while (next < source.size() && std::isspace(static_cast<unsigned char>(source[next])))
                ++next;
            if (next < source.size() && source[next] == ']')
                source[i] = ' ';
        }
        previous = c;
    }
    return Json::parse(source);
}
} // namespace cyuediff::detail
