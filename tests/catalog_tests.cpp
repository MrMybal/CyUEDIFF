#include <cyuediff/catalog.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void file(const fs::path &path, const std::string &data = "not a serialized package") {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << data;
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const Error &e) {
        check(e.code == "catalog-options", "Invalid limits rejected");
        return;
    }
    throw std::runtime_error("Expected catalogue limit error");
}
} // namespace
int main() {
    auto parent = fs::weakly_canonical(fs::current_path());
    auto root = parent / (".test-tmp-catalog-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    auto cleanup = [&] {
        auto path = fs::weakly_canonical(root);
        if (path.parent_path() == parent && path.filename().u8string().rfind(".test-tmp-catalog-", 0) == 0)
            fs::remove_all(path);
    };
    try {
        auto content = root / "Game/Content";
        file(content / fs::u8path("Weapons/Épée.uasset"));
        file(content / "World.umap", "");
        file(content / "World.uasset");
        file(content / "Ignored.uexp");
        file(content / "Ignored.txt");
        file(content / "Upper.UASSET");
        auto plugin = root / "Game/Plugins/Feature/Content";
        file(plugin / "Item.uasset");
        auto engine = root / "EngineRoot";
        file(engine / "Engine/Content/Default.uasset");
        file(engine / "Engine/Plugins/BuiltIn/Content/EnginePlugin.uasset");
        ProjectContext context;
        context.engine.root = engine;
        context.mounts = {{"/Game", content},
                          {"/Feature", plugin},
                          {"/Engine", engine / "Engine/Content"},
                          {"/BuiltIn", engine / "Engine/Plugins/BuiltIn/Content"}};
        auto catalog = catalog_assets(context);
        check(catalog.complete && catalog.assets.size() == 4,
              "Project and plugin packages listed, engine excluded");
        check(catalog.assets[0].path < catalog.assets.back().path, "Deterministic sorted paths");
        auto sword = std::find_if(catalog.assets.begin(), catalog.assets.end(),
                                  [](const auto &a) { return a.package_name == "/Game/Weapons/Épée"; });
        check(sword != catalog.assets.end() && sword->mount == "/Game" && sword->stored_bytes > 0,
              "Unicode package name and size metadata");
        check(std::count_if(catalog.assets.begin(), catalog.assets.end(),
                            [](const auto &a) { return a.package_name == "/Game/World"; }) == 2,
              "Asset and map retain independent paths");
        check(catalog.to_json()["schema"] == "cyuediff.asset-catalog/1" &&
                  catalog.to_json()["assets"].size() == 4,
              "Catalogue JSON contract");
        CatalogOptions options;
        options.include_engine = true;
        check(catalog_assets(context, options).assets.size() == 6,
              "Engine and engine plugin content explicitly included");
        std::reverse(context.mounts.begin(), context.mounts.end());
        check(catalog_assets(context).to_json() == catalog.to_json(),
              "Mount ordering does not change output");
        context.mounts.push_back({"/Alias", content});
        check(catalog_assets(context).assets.size() == 4, "Overlapping mounts deduplicate physical paths");
        context.mounts.pop_back();
        options = {};
        options.max_assets = 1;
        auto limited = catalog_assets(context, options);
        check(!limited.complete && limited.assets.size() == 1 && !limited.diagnostics.empty(),
              "Asset limit produces explicit partial catalogue");
        options = {};
        options.max_entries = 1;
        limited = catalog_assets(context, options);
        check(!limited.complete && limited.visited_entries == 1,
              "Entry budget includes directories and non-assets");
        options = {};
        options.max_path_bytes = 1;
        check(!catalog_assets(context, options).complete &&
                  catalog_assets(context, options).visited_path_bytes == 0,
              "Path memory budget bounds traversal metadata");
        options = {};
        options.max_depth = 0;
        limited = catalog_assets(context, options);
        check(!limited.complete &&
                  std::none_of(limited.assets.begin(), limited.assets.end(),
                               [](const auto &a) { return a.package_name == "/Game/Weapons/Épée"; }),
              "Depth budget prevents recursion");
        options = {};
        options.cancelled = [] { return true; };
        auto cancelled = catalog_assets(context, options);
        check(cancelled.cancelled && !cancelled.complete && cancelled.visited_entries == 0,
              "Cancellation before scanning");
        int calls = 0;
        options.cancelled = [&] { return ++calls > 5; };
        cancelled = catalog_assets(context, options);
        check(cancelled.cancelled && !cancelled.complete, "Cancellation during scanning");
        options = {};
        options.max_assets = 0;
        rejects([&] { catalog_assets(context, options); });
        options = {};
        options.max_entries = 0;
        rejects([&] { catalog_assets(context, options); });
        options = {};
        options.max_path_bytes = 0;
        rejects([&] { catalog_assets(context, options); });
        options = {};
        options.max_depth = 257;
        rejects([&] { catalog_assets(context, options); });
        context.mounts.push_back({"/Missing", root / "does-not-exist"});
        auto missing = catalog_assets(context);
        check(!missing.complete && missing.assets.size() == 4, "Missing mount preserves other results");
        context.mounts.pop_back();
        auto linked = root / "Linked";
        std::error_code error;
        fs::create_directory_symlink(content, linked, error);
        if (!error) {
            context.mounts.push_back({"/Link", linked});
            check(!catalog_assets(context).complete && catalog_assets(context).assets.size() == 4,
                  "Linked mount is not traversed");
            context.mounts.pop_back();
            fs::remove(linked);
        }
        AssetCatalog a, b;
        a.assets = {{"A.uasset", {}, {}, ".uasset"}, {"B.uasset", {}, {}, ".uasset"}};
        b.assets = {{"B.uasset", {}, {}, ".uasset"}, {"C.umap", {}, {}, ".umap"}};
        auto rows = merge_catalogs(a, &b);
        check(rows.size() == 3 && rows[0].before && !rows[0].after && rows[1].before && rows[1].after &&
                  !rows[2].before && rows[2].after,
              "Metadata union preserves presence on both sides");
        check(merge_catalogs(a).size() == 2 && merge_catalogs({}).empty(),
              "Local and empty catalogue unions");
        std::cout << checks << " catalogue checks passed\n";
        cleanup();
    } catch (const std::exception &e) {
        cleanup();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
