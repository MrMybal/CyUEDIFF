#include "../apps/desktop/document.hpp"
#include "container_fixtures.hpp"
#include "zen_fixture.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &error) {
        check(error.code == code, "Expected container model error code");
        return;
    }
    throw std::runtime_error("Expected container model failure");
}
} // namespace
int main() {
    const auto parent = fs::weakly_canonical(fs::current_path());
    auto root = parent / (".test-tmp-desktop-containers-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(root))
        return 1;
    auto cleanup = [&] {
        auto resolved = fs::weakly_canonical(root);
        if (resolved.parent_path() == parent &&
            resolved.filename().u8string().rfind(".test-tmp-desktop-containers-", 0) == 0)
            fs::remove_all(resolved);
    };
    try {
        auto pak = root / fs::u8path("exemple é.pak");
        Bytes bytes;
        bytes.data = pak_plain();
        bytes.save(pak);
        ContainerSource source;
        source.path = pak;
        auto listing = load_container_document(source, root / "not-loaded.dll");
        check(listing->input->entries().size() == 3 && listing->inspection.is_null() && !listing->zen,
              "Index-only request does not decode members or load a codec");
        check(listing->metadata["kind"] == "pak" && !listing->metadata.contains("entries"),
              "Bounded metadata listing");
        source.entry = "Content/A.bin";
        auto member = load_container_document(source, {});
        check(member->inspection["decodedBytes"] == 6 &&
                  member->inspection["prefixHex"] == "61 62 63 64 65 66",
              "Member preview is a bounded byte prefix");
        check(member->inspection["decodedSha256"] ==
                  sha256_bytes(std::vector<std::uint8_t>({'a', 'b', 'c', 'd', 'e', 'f'})),
              "Decoded member SHA256");
        check(!member->zen && member->report()["zen"].is_null(),
              "Generic Pak member does not claim cooked coverage");
        source.entry = "Content/Gone.bin";
        rejects([&] { load_container_document(source, {}); }, "desktop-container");
        source.entry = "missing";
        rejects([&] { load_container_document(source, {}); }, "container-entry");
        source.entry = "Content/A.bin";
        source.max_payload_bytes = 5;
        rejects([&] { load_container_document(source, {}); }, "container-limit");
        source.max_payload_bytes = 0;
        rejects([&] { load_container_document(source, {}); }, "desktop-container");
        source = {};
        rejects([&] { load_container_document(source, {}); }, "desktop-container");
        auto payload = zen_fixture();
        payload.save(root / "test.ucas");
        zen_toc(payload).save(root / "test.utoc");
        auto scripts = script_fixture();
        scripts.save(root / "global.ucas");
        zen_toc(scripts, 5).save(root / "global.utoc");
        source.path = root / "test.utoc";
        auto index = load_container_document(source, {});
        source.entry = index->input->entries().front().id;
        member = load_container_document(source, {});
        check(!member->zen && member->inspection["coverage"] == "decoded-bytes", "No cooked layout guessed");
        source.profile = ZenProfile::unreal_5_3;
        source.script_objects = root / "global.utoc";
        member = load_container_document(source, {});
        check(member->zen && member->zen->exports.size() == 2 &&
                  member->zen->exports[0].class_name == "/Script/Test.DemoAsset",
              "Explicit Zen profile and script objects");
        check(member->zen->sha256 == member->inspection["decodedSha256"] &&
                  member->zen->property_exports_decoded == 0,
              "Opaque body evidence retained without mappings");
        check(std::find(member->protected_files.begin(), member->protected_files.end(),
                        root / "global.ucas") != member->protected_files.end(),
              "Global partition inputs are protected");
        auto source_hash = sha256_file(root / "test.ucas");
        LoadRequest request;
        request.container = source;
        auto document = load_document(request);
        check(document->status() == "ready" && document->container && !document->before &&
                  document->previews.empty(),
              "Container document uses normal background loader without classic packages");
        check(document->report()["container"]["zen"]["name"] == "/Game/Example",
              "Container report retains cooked package metadata");
        request.before = "conflict.uasset";
        rejects([&] { load_document(request); }, "desktop-container");
        bool cancel = false;
        OperationControl control;
        control.cancelled = [&] { return cancel; };
        control.progress = [&](const auto &) { cancel = true; };
        rejects([&] { load_container_document(source, {}, control); }, "cancelled");
        check(sha256_file(root / "test.ucas") == source_hash,
              "Successful reads and cancellation leave original bytes unchanged");
        auto changed = payload;
        int header;
        std::memcpy(&header, changed.data.data() + 4, 4);
        changed.data[header] = 'X';
        changed.save(root / "test.ucas");
        auto refreshed = load_container_document(source, {});
        check(refreshed->zen->sha256 != member->zen->sha256, "Every inspection reopens current source bytes");
        source.script_objects = root / "test.utoc";
        rejects([&] { load_container_document(source, {}); }, "zen-entry");
        source.script_objects.clear();
        source.profile = ZenProfile::unreal_5_5;
        // The other supported profile must reject a mismatched 5.3 header rather than guess.
        rejects([&] { load_container_document(source, {}); }, "zen-format");
        cleanup();
        std::cout << checks << " desktop container checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        cleanup();
        return 1;
    }
}
