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
        auto pak = root / fs::u8path("exemple ÃƒÂ©.pak");
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
        source = {};
        source.path = root / "test.utoc";
        source.after_path = root / "test.utoc";
        auto pair_index = load_container_document(source, root / "not-loaded.dll");
        check(pair_index->members.size() == 1 && pair_index->member_states[0] == "unchecked" &&
                  pair_index->comparison.is_null(),
              "Index pairing never claims decoded equality");
        payload.save(root / "before.ucas");
        zen_toc(payload).save(root / "before.utoc");
        source.path = root / "before.utoc";
        source.entry = "id:000000000000000000000001";
        source.profile = source.after_profile = ZenProfile::unreal_5_3;
        source.script_objects = source.after_script_objects = root / "global.utoc";
        auto pair = load_container_document(source, {});
        check(pair->member_states[0] == "modified" && pair->selected_comparison["zen"]["status"] == "changed",
              "Selected Zen pair locates changed opaque object bodies");
        check(pair->selected_comparison["zen"] == compare_zen_packages(*pair->zen, *pair->after->zen),
              "Desktop selected Zen comparison is identical to Core");
        check(pair->comparison.is_null() && pair->after->source.profile == ZenProfile::unreal_5_3,
              "Independent explicit after context without a full payload scan");
        source.compare_payloads = true;
        pair = load_container_document(source, {});
        check(pair->comparison == compare_containers(*pair->input, *pair->after->input),
              "Full container comparison reuses Core decoded-byte evidence");
        check(std::find(pair->protected_files.begin(), pair->protected_files.end(), root / "before.ucas") !=
                      pair->protected_files.end() &&
                  std::find(pair->protected_files.begin(), pair->protected_files.end(), root / "test.ucas") !=
                      pair->protected_files.end(),
              "Both physical container payloads are protected");
        source.max_comparison_bytes = 1;
        rejects([&] { load_container_document(source, {}); }, "container-limit");
        source.max_comparison_bytes = 0;
        rejects([&] { load_container_document(source, {}); }, "desktop-container");
        source.max_comparison_bytes = 512ull * 1024 * 1024;
        source.compare_payloads = false;
        source.after_profile = ZenProfile::unreal_5_5;
        rejects([&] { load_container_document(source, {}); }, "zen-format");
        source = {};
        source.path = pak;
        source.after_path = root / "test.utoc";
        source.entry = "path:Content/A.bin";
        pair = load_container_document(source, {});
        check(pair->members.size() == 3 && pair->selected_comparison["state"] == "removed" &&
                  pair->after->inspection.is_null(),
              "Removed member is inspected on the before side only");
        source.entry = "id:000000000000000000000001";
        pair = load_container_document(source, {});
        check(pair->selected_comparison["state"] == "added" && pair->inspection.is_null() &&
                  !pair->after->inspection.is_null(),
              "Added member is inspected on the after side only");
        source.entry = "missing";
        rejects([&] { load_container_document(source, {}); }, "container-entry");
        source.entry.clear();
        source.after_path.clear();
        source.compare_payloads = true;
        rejects([&] { load_container_document(source, {}); }, "desktop-container");
        cleanup();
        std::cout << checks << " desktop container checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        cleanup();
        return 1;
    }
}
