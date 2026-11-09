#include <cyuediff/core.hpp>
#include "../src/json_output.hpp"
#include "../src/atomic_output.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace cyuediff;
namespace {
unsigned checks = 0;
void check(bool v, const char *why) {
    ++checks;
    if (!v)
        throw std::runtime_error(why);
}
std::string read(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
} // namespace
int main() {
    fs::path root;
    try {
        DiffResult d;
        d.binary_comparison_available = true;
        d.binary_changed = true;
        d.changes.push_back({{"op", "replace"},
                             {"path", "/some~1field"},
                             {"before", std::int64_t(9007199254740993LL)},
                             {"after", "caf\xc3\xa9 \xf0\x9f\x98\x80 \"\n"}});
        d.unavailable_scopes.push_back(
            {{"path", "/missing"}, {"beforeUnavailable", false}, {"afterUnavailable", true}});
        d.source_contexts = {{"before", {{"type", "a"}}}, {"after", {{"type", "b"}}}, {"changed", true}};
        for (int indent : {-1, 0, 2, 8}) {
            std::ostringstream out;
            d.write_json(out, indent);
            check(out.str() == d.to_json().dump(indent, ' ', false, Json::error_handler_t::replace),
                  "Streaming report retains exact materialized JSON formatting and facts");
        }
        Json extra = {{"host", {{"name", "test"}}}, {"readLimits", {{"before", 1}, {"after", 2}}}};
        std::ostringstream out;
        d.write_json(out, 2, extra);
        auto expected = d.to_json();
        for (auto i = extra.begin(); i != extra.end(); ++i)
            expected[i.key()] = i.value();
        check(Json::parse(out.str()) == expected,
              "Additional report provenance is merged without copying result arrays");
        for (const auto *key : {"schema", "status", "changes", "sourceReadLimitFailed", "unavailableScopes",
                                "sourceContexts"}) {
            std::ostringstream invalid;
            try {
                DiffResult{}.write_json(invalid, 2, Json{{key, "override"}});
                throw std::runtime_error("Expected reserved field rejection");
            } catch (const Error &e) {
                check(e.code == "report-field" && invalid.str().empty(),
                      "Reserved report fields reject before any output, including inactive optional fields");
            }
        }
        for (int indent : {-2, 17}) {
            std::ostringstream invalid;
            try {
                d.write_json(invalid, indent);
                throw std::runtime_error("Expected indentation rejection");
            } catch (const Error &e) {
                check(e.code == "report-format" && invalid.str().empty(),
                      "Invalid writer options fail before output");
            }
        }
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        std::ostringstream stopped;
        try {
            d.write_json(stopped, 2, Json::object(), cancel);
            throw std::runtime_error("Expected cancelled writer");
        } catch (const Error &e) {
            check(e.code == "cancelled" && stopped.str().empty(),
                  "Report cancellation is checked before emitting bytes");
        }
        Json value = Json::array({1});
        detail::JsonObject view;
        view.borrow("borrowed", value);
        unsigned calls = 0;
        view.array("generated", 3, [&](std::size_t i) {
            ++calls;
            return Json{{"item", i}};
        });
        check(!calls, "Report producers are deferred until output");
        value.push_back(2);
        std::ostringstream borrowed;
        view.write(borrowed);
        auto parsed = Json::parse(borrowed.str());
        check(calls == 3 && parsed["borrowed"].size() == 2 && parsed["generated"][2]["item"] == 2,
              "Borrowed data stays current and generated array items are visited once");
        check(view.to_json() == parsed, "Explicit owned materialization retains the same report contract");
        const auto cwd = fs::current_path();
        root = cwd / (".test-tmp-stream-report-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(root.parent_path() == cwd && fs::create_directory(root),
              "Create an isolated owned test output directory");
        const auto target = root / "report.json";
        detail::atomic_output(target, [&](std::ostream &stream) { d.write_json(stream); });
        const auto original = read(target);
        check(Json::parse(original) == d.to_json(), "Atomic JSON output publishes complete parsed facts");
        try {
            detail::atomic_output(target, [](std::ostream &stream) {
                stream << "partial";
                throw Error("test-abort", "abort");
            });
            throw std::runtime_error("Expected generation failure");
        } catch (const Error &e) {
            check(e.code == "test-abort" && read(target) == original,
                  "Failed generation preserves the previous destination");
        }
        try {
            detail::atomic_output(target, [](std::ostream &stream) {
                stream << "partial";
                stream.setstate(std::ios::badbit);
            });
            throw std::runtime_error("Expected stream failure");
        } catch (const Error &e) {
            check(e.code == "io" && read(target) == original,
                  "Failed output state preserves the previous destination");
        }
        try {
            detail::atomic_output(root / "missing.json", [](std::ostream &stream) {
                stream << "partial";
                throw Error("test-abort", "abort");
            });
            throw std::runtime_error("Expected unpublished failure");
        } catch (const Error &) {
            check(!fs::exists(root / "missing.json"), "Failed new output never publishes a partial document");
        }
        d.source_read_limit_failed = true;
        detail::atomic_output(target, [&](std::ostream &stream) { d.write_json(stream); });
        check(Json::parse(read(target)).at("sourceReadLimitFailed") == true,
              "Existing output can be atomically replaced with a new complete report");
        const auto directory = root / "directory";
        fs::create_directory(directory);
        bool generated = false;
        try {
            detail::atomic_output(directory, [&](std::ostream &) { generated = true; });
            throw std::runtime_error("Expected directory output failure");
        } catch (const Error &e) {
            check(e.code == "io" && !generated && fs::is_directory(directory),
                  "Directory targets reject before data generation or temporary output");
        }
        fs::remove(directory);
        unsigned files = 0;
        for (const auto &entry : fs::directory_iterator(root)) {
            ++files;
            check(entry.path() == target,
                  "No sibling temporary files remain after successful or failed output");
        }
        check(files == 1, "Only the requested report remains");
        check(root.parent_path() == fs::current_path(),
              "Verify test cleanup stays within its intended workspace");
        fs::remove_all(root);
        root.clear();
        std::cout << checks << " streaming/atomic report checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        if (!root.empty() && root.parent_path() == fs::current_path()) {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
