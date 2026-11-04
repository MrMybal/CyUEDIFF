#include <cyuediff/core.hpp>
#include <cyuediff/catalog.hpp>
#include <cyuediff/hdr.hpp>
#include <cyuediff/container.hpp>
#include <cmath>
#include <locale>
#include <sstream>
#ifdef CYUEDIFF_HAS_GIT
#include <cyuediff/git.hpp>
#endif
#include <fstream>
#include <iostream>
#include <set>
#include <limits>
using namespace cyuediff;
namespace {
int run(const std::vector<std::string> &args) {
    if (args.empty() || args[0] == "--help" || args[0] == "help") {
        std::cout
            << "CyUEDiff " CYUEDIFF_VERSION " - standalone Unreal package reader\n"
               "  cyuediff inspect ASSET [--metadata] [--snapshot] [--project PROJECT] [--engine-root "
               "ROOT] [--out FILE]\n"
               "  cyuediff diff BEFORE AFTER [--project PROJECT] [--before-project PROJECT] "
               "[--after-project PROJECT] [--engine-root ROOT] [--out FILE]\n"
               "  cyuediff graph ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff texture ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff preview ASSET --out IMAGE.png [--mip N] [--object NAME]\n"
               "  cyuediff image-diff BEFORE AFTER [--threshold 0..255] [--mip N] [--object NAME] "
               "[--heatmap MASK.png] [--out REPORT.json]\n"
               "  cyuediff hdr-diff BEFORE AFTER [--threshold SAMPLE_DELTA] [--heatmap MASK.png] [--codec "
               "MODULE.dll]\n"
               "  preview accepts --exposure EV for HDR source display.\n"
               "  cyuediff material ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff material-diff BEFORE AFTER [--out FILE]\n"
               "  cyuediff mesh ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff geometry ASSET [--lod N] [--object NAME] [--codec MODULE.dll] [--out FILE]\n"
               "  cyuediff export-mesh ASSET --out MESH.obj [--lod N] [--object NAME] [--codec MODULE.dll]\n"
               "  cyuediff mesh-diff BEFORE AFTER [--lod N] [--object NAME] [--codec MODULE.dll] [--out "
               "FILE]\n"
               "  preview and image-diff also accept --codec MODULE.dll\n"
#ifdef CYUEDIFF_HAS_GIT
               "  cyuediff git-catalog REPO REV [--max-assets N] [--max-entries N] [--out FILE]\n"
               "  cyuediff git-project REPO REV PROJECT [--engine-root ROOT] [--out FILE]\n"
               "  cyuediff git-inspect REPO REV PATH [--out FILE] [--lfs-objects DIR]\n"
               "  cyuediff git-diff REPO BEFORE AFTER PATH [--after-path PATH] [--out FILE]\n"
               "  Git commands also accept --lfs-objects DIR and --git-temp DIR.\n"
               "  git-inspect/git-diff accept --project REPO_PROJECT and long package names.\n"
               "  git-diff accepts --before-project/--after-project and "
               "--before-engine-root/--after-engine-root.\n"
               "  --no-engine skips engine discovery for Git project contexts.\n"
#endif
               "  cyuediff container INPUT.pak|INPUT.utoc [--out REPORT.json]\n"
               "  cyuediff container-read INPUT ENTRY_PATH_OR_ID --out BYTES.bin [--codec MODULE.dll]\n"
               "  cyuediff container-hash INPUT [--codec MODULE.dll] [--out REPORT.json]\n"
               "  cyuediff container-diff BEFORE AFTER [--codec MODULE.dll] [--out REPORT.json]\n"
               "  Container comparisons cover decoded bytes, not cooked object semantics.\n"
               "  cyuediff textconv ASSET\n"
               "  cyuediff catalog PROJECT [--include-engine] [--max-assets N] [--max-entries N] [--out "
               "FILE]\n"
               "  cyuediff project PROJECT [--engine-root ROOT]\n"
               "  cyuediff resolve PROJECT /Game/Package [--engine-root ROOT]\n"
               "Diff exit codes: 0 identical, 1 changes, 2 error/incomplete/unverified.\n";
        return 0;
    }
    std::vector<std::string> positional;
    Json flags = Json::object();
    const std::set<std::string> valued = {"--max-assets",
                                          "--max-entries",
                                          "--exposure",
                                          "--project",
                                          "--before-project",
                                          "--after-project",
                                          "--engine-root",
                                          "--out",
                                          "--mip",
                                          "--object",
                                          "--threshold",
                                          "--heatmap",
                                          "--lod",
                                          "--codec",
                                          "--lfs-objects",
                                          "--git-temp",
                                          "--after-path",
                                          "--before-engine-root",
                                          "--after-engine-root"};
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto &arg = args[i];
        if (valued.count(arg)) {
            if (i + 1 == args.size())
                throw Error("arguments", "Missing value for " + arg);
            flags[arg] = args[++i];
        } else if (arg == "--metadata" || arg == "--snapshot" || arg == "--no-engine" ||
                   arg == "--include-engine")
            flags[arg] = true;
        else if (arg.compare(0, 2, "--") == 0)
            throw Error("arguments", "Unknown option " + arg);
        else
            positional.push_back(arg);
    }
    auto path_option = [&](const std::string &key) { return fs::u8path(flags.value(key, std::string{})); };
    const auto command = args[0];
    auto required = (command == "diff" || command == "resolve" || command == "image-diff" ||
                     command == "mesh-diff" || command == "material-diff" || command == "hdr-diff" ||
                     command == "container-read" || command == "container-diff")
                        ? 2u
                        : 1u;
    const bool git_command = command == "git-inspect" || command == "git-diff" || command == "git-project" ||
                             command == "git-catalog";
    const bool container_command = command == "container" || command == "container-read" ||
                                   command == "container-hash" || command == "container-diff";
    const bool catalog_command = command == "catalog" || command == "git-catalog";
    if (git_command)
        required = command == "git-diff" ? 4u : (command == "git-catalog" ? 2u : 3u);
    if (positional.size() != required)
        throw Error("arguments", "Wrong number of arguments; use --help");
    const bool image_command = command == "preview" || command == "image-diff" || command == "hdr-diff";
    const bool geometry_command = command == "geometry" || command == "export-mesh" || command == "mesh-diff";
    for (auto it = flags.begin(); it != flags.end(); ++it) {
        const auto &key = it.key();
        if (container_command) {
            if (key != "--out" && !(key == "--codec" && command != "container"))
                throw Error("arguments", "Unsupported container option: " + key);
            continue;
        }
        const bool side_context = key == "--before-project" || key == "--after-project" ||
                                  key == "--before-engine-root" || key == "--after-engine-root";
        if ((key == "--max-assets" || key == "--max-entries") && !catalog_command)
            throw Error("arguments", key + " requires a catalogue command");
        if (key == "--include-engine" && command != "catalog")
            throw Error("arguments", "--include-engine requires catalog");
        if (catalog_command && key != "--out" && key != "--max-assets" && key != "--max-entries" &&
            !(command == "catalog" && (key == "--engine-root" || key == "--include-engine")) &&
            !(command == "git-catalog" && key == "--git-temp"))
            throw Error("arguments", "Unsupported catalogue option: " + key);
        if (git_command && !catalog_command && key != "--out" && key != "--lfs-objects" &&
            key != "--git-temp" && key != "--engine-root" && key != "--no-engine" &&
            !(key == "--project" && command != "git-project") &&
            !(command == "git-diff" && (side_context || key == "--after-path")))
            throw Error("arguments", "Unsupported option for Git inspection: " + key);
        if (!git_command &&
            (key == "--lfs-objects" || key == "--git-temp" || key == "--after-path" || key == "--no-engine" ||
             key == "--before-engine-root" || key == "--after-engine-root"))
            throw Error("arguments", key + " requires a Git command");
        if (((key == "--mip") && !image_command) ||
            (key == "--object" && !image_command && !geometry_command))
            throw Error("arguments", key + " requires an image or geometry command");
        if ((key == "--threshold" || key == "--heatmap") && command != "image-diff" && command != "hdr-diff")
            throw Error("arguments", key + " requires image-diff or hdr-diff");
        if (key == "--exposure" && command != "preview")
            throw Error("arguments", "--exposure requires preview");
        if ((key == "--lod" && !geometry_command) ||
            (key == "--codec" && !geometry_command && !image_command))
            throw Error("arguments", key + " requires a source decode command");
        if ((image_command || geometry_command) && (key == "--metadata" || key == "--snapshot"))
            throw Error("arguments", key + " cannot be used with image decoding");
    }
    auto integer_option = [&](const std::string &key, std::uint32_t limit) {
        auto text = flags.value(key, std::string("0"));
        std::uint64_t value = 0;
        if (text.empty())
            throw Error("arguments", "Empty value for " + key);
        for (char c : text) {
            if (c < '0' || c > '9' || value > limit / 10 ||
                (value == limit / 10 && static_cast<unsigned>(c - '0') > limit % 10))
                throw Error("arguments", "Invalid or out of range value for " + key);
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        if (value > limit)
            throw Error("arguments", "Out of range value for " + key);
        return static_cast<std::uint32_t>(value);
    };
    auto real_option = [&](const std::string &key, double fallback, double minimum, double maximum) {
        if (!flags.contains(key))
            return fallback;
        std::istringstream input(flags.at(key).get<std::string>());
        input.imbue(std::locale::classic());
        double value = 0;
        input >> value;
        if (!input || input.peek() != std::char_traits<char>::eof() || !std::isfinite(value) ||
            value < minimum || value > maximum)
            throw Error("arguments", "Invalid or out of range value for " + key);
        return value;
    };
    auto catalog_options = [&] {
        CatalogOptions result;
        result.include_engine = flags.contains("--include-engine");
        if (flags.contains("--max-assets"))
            result.max_assets = integer_option("--max-assets", 1'000'000);
        if (flags.contains("--max-entries"))
            result.max_entries = integer_option("--max-entries", 10'000'000);
        return result;
    };
    auto select_texture = [&](const Package &package) -> const TextureSource & {
        auto name = flags.value("--object", std::string{});
        const TextureSource *selected = nullptr;
        for (const auto &texture : package.textures) {
            auto leaf = texture.object_path.substr(texture.object_path.find_last_of('.') + 1);
            if (!name.empty() && name != texture.object_path && name != leaf)
                continue;
            if (selected)
                throw Error("texture-selection",
                            "Several textures match; specify their full path with --object");
            selected = &texture;
        }
        if (!selected)
            throw Error("texture-selection", "No matching decoded Texture2D source in this package");
        return *selected;
    };
    auto root = path_option("--engine-root");
    std::vector<fs::path> protected_files, protected_directories;
    auto read = [&](const std::string &input, const std::string &project_key, ReadOptions options) {
        auto project = path_option(project_key);
        if (project.empty())
            project = path_option("--project");
        fs::path asset = fs::u8path(input);
        if (!project.empty()) {
            auto context = ProjectContext::open(project, root);
            protected_files.push_back(context.project_file);
            if (input.rfind("/Game/", 0) == 0 || input.rfind("/Engine/", 0) == 0 ||
                (!fs::exists(asset) && input[0] == '/'))
                asset = context.resolve(input);
        }
        protected_files.push_back(asset);
        for (const auto *ext : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
            auto f = asset;
            f.replace_extension(ext);
            protected_files.push_back(f);
        }
        return Package::open(asset, options);
    };
    BlockDecoder decoder;
    auto codec = path_option("--codec");
    if (!codec.empty()) {
        decoder = load_block_decoder(codec);
        protected_files.push_back(codec);
    }
    TextureDecodeOptions texture_options;
    texture_options.decoder = decoder;
    MeshDecodeOptions mesh_options;
    mesh_options.decoder = decoder;
    Json output;
    std::vector<std::uint8_t> raw_bytes;
    fs::path raw_destination;
    MeshGeometry obj;
    fs::path obj_destination;
    ImageRgba8 png;
    fs::path png_destination;
    int exit_code = 0;
    ReadOptions options;
    if (container_command) {
        auto input = Container::open(fs::u8path(positional[0]));
        for (const auto &file : input.files())
            protected_files.push_back(file);
        ContainerReadOptions decode_options;
        if (decoder)
            decode_options.decoder = container_block_decoder(decoder);
        if (command == "container-diff") {
            auto after = Container::open(fs::u8path(positional[1]));
            for (const auto &file : after.files())
                protected_files.push_back(file);
            output = compare_containers(input, after, decode_options);
            exit_code = output.at("status") == "identical" ? 0 : 1;
        } else if (command == "container-read") {
            raw_destination = path_option("--out");
            if (raw_destination.empty())
                throw Error("arguments", "container-read requires --out");
            std::size_t found = input.entries().size();
            for (std::size_t j = 0; j < input.entries().size(); ++j) {
                const auto &e = input.entries()[j];
                if (e.id == positional[1] || (!e.path.empty() && e.path == positional[1])) {
                    if (found != input.entries().size())
                        throw Error("container-entry", "Ambiguous member name");
                    found = j;
                }
            }
            if (found == input.entries().size())
                throw Error("container-entry", "No matching container member");
            raw_bytes = input.read(found, decode_options);
            output = {{"schema", "cyuediff.container-read/1"},
                      {"entry", input.entries()[found].to_json()},
                      {"size", raw_bytes.size()},
                      {"sha256", sha256_bytes(raw_bytes)},
                      {"coverage", "decoded-bytes"}};
        } else {
            output = input.to_json();
            if (command == "container-hash") {
                output["decodedSha256"] = Json::array();
                for (std::size_t j = 0; j < input.entries().size(); ++j) {
                    const auto &e = input.entries()[j];
                    if (!e.deleted)
                        output["decodedSha256"].push_back(
                            {{"id", e.id}, {"sha256", sha256_bytes(input.read(j, decode_options))}});
                }
            }
        }
    } else if (git_command) {
#ifdef CYUEDIFF_HAS_GIT
        GitOptions git_options;
        git_options.temporary_root = path_option("--git-temp");
        git_options.lfs_objects = path_option("--lfs-objects");
        auto repo = GitRepository::open(fs::u8path(positional[0]), git_options);
        protected_directories.push_back(repo.common_directory());
        protected_directories.push_back(repo.lfs_objects());
        protected_files.push_back(repo.root() / ".git");
        auto protect = [&](const std::string &path) {
            auto asset = repo.root() / fs::u8path(path);
            protected_files.push_back(asset);
            for (const auto *ext : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
                auto companion = asset;
                companion.replace_extension(ext);
                protected_files.push_back(companion);
            }
        };
        auto project_options = [&](const std::string &side) {
            GitProjectOptions result;
            result.engine_root = path_option(side);
            if (result.engine_root.empty())
                result.engine_root = root;
            result.discover_engine = !flags.contains("--no-engine");
            if (!result.discover_engine && !result.engine_root.empty())
                throw Error("arguments", "--no-engine conflicts with an explicit engine root");
            return result;
        };
        auto protect_context = [&](const GitProjectContext &context) {
            for (const auto &document : context.documents)
                protected_files.push_back(document.file.blob_oid.empty()
                                              ? fs::u8path(document.file.path)
                                              : repo.root() / fs::u8path(document.file.path));
            if (!context.engine.root.empty())
                protected_directories.push_back(context.engine.root / "Engine");
        };
        auto protect_revision = [&](const GitRevision &revision) {
            if (revision.storage == "git")
                protect(revision.repository_path);
            else
                for (const auto &file : revision.files)
                    protected_files.push_back(fs::u8path(file.path));
        };
        if (command == "git-catalog") {
            auto catalog = repo.catalog(positional[1], catalog_options());
            for (const auto &asset : catalog.assets)
                protect(asset.path);
            output = catalog.to_json();
            if (!catalog.complete)
                exit_code = 2;
        } else if (command == "git-project") {
            auto context = repo.project(positional[1], positional[2], project_options("--engine-root"));
            protect_context(context);
            output = context.to_json();
            if (!context.complete)
                exit_code = 2;
        } else if (command == "git-inspect") {
            GitRevision revision;
            auto project = flags.value("--project", std::string{});
            Json context = nullptr;
            if (!project.empty()) {
                auto selected = repo.project(positional[1], project, project_options("--engine-root"));
                protect_context(selected);
                revision = repo.materialize(selected, positional[2]);
                context = selected.to_json();
                if (!selected.complete)
                    exit_code = 2;
            } else {
                if (flags.contains("--engine-root") || flags.contains("--no-engine"))
                    throw Error("arguments", "Engine selection requires a Git project context");
                revision = repo.materialize(positional[1], positional[2]);
            }
            protect_revision(revision);
            auto package = revision.open(options);
            output = package.to_json();
            output["revision"] = revision.to_json();
            if (!context.is_null())
                output["projectContext"] = context;
            if (!package.complete_input)
                exit_code = 2;
        } else {
            auto project = flags.value("--project", std::string{});
            auto a = flags.value("--before-project", project), b = flags.value("--after-project", project);
            GitComparison comparison;
            if (!a.empty() || !b.empty()) {
                if (a.empty() || b.empty())
                    throw Error("arguments", "Specify a historical project for both sides");
                auto before = repo.project(positional[1], a, project_options("--before-engine-root"));
                auto after = repo.project(positional[2], b, project_options("--after-engine-root"));
                protect_context(before);
                protect_context(after);
                comparison = compare_git(repo, before, after, positional[3],
                                         flags.value("--after-path", std::string{}), options);
            } else {
                if (flags.contains("--engine-root") || flags.contains("--before-engine-root") ||
                    flags.contains("--after-engine-root") || flags.contains("--no-engine"))
                    throw Error("arguments", "Engine selection requires historical project contexts");
                comparison = compare_git(repo, positional[1], positional[2], positional[3],
                                         flags.value("--after-path", std::string{}), options);
            }
            protect_revision(comparison.before);
            protect_revision(comparison.after);
            output = comparison.to_json();
            auto status = comparison.status();
            exit_code =
                status == "identical" ? 0 : ((status == "incomplete" || status == "unverified") ? 2 : 1);
        }
#else
        throw Error("git-unavailable", "Configure CYUEDIFF_BUILD_GIT=ON to enable Git commands");
#endif
    } else if (command == "inspect" || command == "textconv" || command == "graph" || command == "texture" ||
               command == "mesh" || command == "material") {
        if ((command == "graph" || command == "texture" || command == "mesh" || command == "material") &&
            flags.contains("--metadata"))
            throw Error("arguments", "Graph/texture inspection requires properties");
        if (flags.contains("--metadata"))
            options.properties = options.fingerprints = false;
        auto package = read(positional[0], "--project", options);
        if (command == "graph")
            output = package.blueprint_snapshot();
        else if (command == "texture")
            output = package.texture_snapshot();
        else if (command == "mesh")
            output = package.mesh_snapshot();
        else if (command == "material")
            output = package.material_snapshot();
        else
            output = (command == "textconv" || flags.contains("--snapshot")) ? package.semantic_snapshot()
                                                                             : package.to_json();
        if (!package.complete_input)
            exit_code = 2;
    } else if (image_command) {
        auto before = read(positional[0], "--before-project", options);
        if (!before.complete_input)
            throw Error("incomplete", "Required input payload is missing");
        auto mip = integer_option("--mip", 31);
        const auto &source = select_texture(before);
        if (command == "hdr-diff") {
            auto a = decode_texture_hdr(source, mip, texture_options);
            auto after = read(positional[1], "--after-project", options);
            if (!after.complete_input)
                throw Error("incomplete", "Required input payload is missing");
            auto b = decode_texture_hdr(select_texture(after), mip, texture_options);
            png_destination = path_option("--heatmap");
            auto comparison =
                compare_hdr_images(a, b, real_option("--threshold", 0, 0, std::numeric_limits<double>::max()),
                                   !png_destination.empty());
            output = comparison.to_json();
            output["mip"] = mip;
            exit_code = comparison.comparable && !comparison.changed_pixels ? 0 : 1;
            png = std::move(comparison.heatmap);
        } else if (command == "preview") {
            bool hdr = is_hdr_format(source.format);
            auto exposure = real_option("--exposure", 0, -32, 32);
            if (flags.contains("--exposure") && !hdr)
                throw Error("arguments", "--exposure requires an HDR source");
            auto image = hdr ? tone_map_hdr(decode_texture_hdr(source, mip, texture_options), exposure)
                             : decode_texture(source, mip, texture_options);
            png_destination = path_option("--out");
            if (png_destination.empty())
                throw Error("arguments", "preview requires --out IMAGE.png");
            output = {{"schema", "cyuediff.preview/1"},
                      {"width", image.width},
                      {"height", image.height},
                      {"mip", mip},
                      {"path", png_destination.u8string()},
                      {"sourceFormat", source.format},
                      {"conversion", hdr ? "HDR display: exposure, positive Reinhard tone mapping, "
                                           "linear-to-sRGB RGB; clamped alpha"
                                         : "RGBA8 encoded samples; no color-space conversion"}};
            if (hdr)
                output["exposureEV"] = exposure;
            png = std::move(image);
        } else {
            auto after = read(positional[1], "--after-project", options);
            if (!after.complete_input)
                throw Error("incomplete", "Required input payload is missing");
            const auto &other_source = select_texture(after);
            if (is_hdr_format(source.format) || is_hdr_format(other_source.format))
                throw Error("hdr-comparison",
                            "Use hdr-diff to compare HDR source samples before display conversion");
            auto image = decode_texture(source, mip, texture_options),
                 other = decode_texture(other_source, mip, texture_options);
            png_destination = path_option("--heatmap");
            auto comparison =
                compare_images(image, other, static_cast<std::uint8_t>(integer_option("--threshold", 255)),
                               !png_destination.empty());
            output = comparison.to_json();
            output["mip"] = mip;
            exit_code = comparison.comparable && !comparison.changed_pixels ? 0 : 1;
            png = std::move(comparison.heatmap);
        }
    } else if (geometry_command) {
        auto before = read(positional[0], "--before-project", options);
        if (!before.complete_input)
            throw Error("incomplete", "Required input payload is missing");
        auto lod = integer_option("--lod", 1000000);
        auto object = flags.value("--object", std::string{});
        auto geometry = decode_mesh(before.mesh_source(object, lod), mesh_options);
        if (command == "geometry")
            output = geometry.to_json();
        else if (command == "export-mesh") {
            obj_destination = path_option("--out");
            if (obj_destination.empty())
                throw Error("arguments", "export-mesh requires --out MESH.obj");
            output = {{"schema", "cyuediff.obj-export/1"},
                      {"vertices", geometry.vertices.size()},
                      {"instances", geometry.instances.size()},
                      {"triangles", geometry.triangles.size()},
                      {"lod", lod},
                      {"path", obj_destination.u8string()}};
            obj = std::move(geometry);
        } else {
            auto after = read(positional[1], "--after-project", options);
            if (!after.complete_input)
                throw Error("incomplete", "Required input payload is missing");
            auto other = decode_mesh(after.mesh_source(object, lod), mesh_options);
            auto comparison = compare_meshes(geometry, other);
            output = comparison.to_json();
            output["lod"] = lod;
            exit_code = comparison.changes.empty() ? 0 : 1;
        }
    } else if (command == "material-diff") {
        if (flags.contains("--metadata"))
            throw Error("arguments", "Material comparison requires properties");
        auto before = read(positional[0], "--before-project", options);
        auto after = read(positional[1], "--after-project", options);
        auto comparison = compare_materials(before, after);
        output = comparison.to_json();
        exit_code = !comparison.complete ? 2 : (comparison.changes.empty() ? 0 : 1);
    } else if (command == "diff") {
        if (flags.contains("--metadata"))
            throw Error("arguments", "Diff requires fingerprints and property reading");
        auto before = read(positional[0], "--before-project", options),
             after = read(positional[1], "--after-project", options);
        auto result = compare(before, after);
        output = result.to_json();
        output["beforeDiagnostics"] = before.diagnostics;
        output["afterDiagnostics"] = after.diagnostics;
        exit_code = result.status() == "identical"
                        ? 0
                        : ((result.status() == "incomplete" || result.status() == "unverified") ? 2 : 1);
    } else if (command == "catalog") {
        auto context = ProjectContext::open(fs::u8path(positional[0]), root);
        auto catalog = catalog_assets(context, catalog_options());
        protected_files.push_back(context.project_file);
        for (const auto &mount : context.mounts)
            protected_directories.push_back(mount.directory);
        protected_directories.push_back(context.project_file.parent_path() / "Plugins");
        if (!context.engine.root.empty())
            protected_directories.push_back(context.engine.root / "Engine");
        for (const auto &asset : catalog.assets) {
            auto path = fs::u8path(asset.path);
            protected_files.push_back(path);
            for (const auto *extension : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
                auto companion = path;
                companion.replace_extension(extension);
                protected_files.push_back(companion);
            }
        }
        output = catalog.to_json();
        if (!catalog.complete)
            exit_code = 2;
    } else if (command == "project" || command == "resolve") {
        auto context = ProjectContext::open(fs::u8path(positional[0]), root);
        protected_files.push_back(context.project_file);
        output = command == "project" ? context.to_json()
                                      : Json{{"path", context.resolve(positional[1]).u8string()}};
    } else
        throw Error("arguments", "Unknown command " + command);
    auto text = output.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    auto destination = (command == "preview" || command == "export-mesh" || command == "container-read")
                           ? fs::path{}
                           : path_option("--out");
    auto same_file = [](const fs::path &a, const fs::path &b) {
        return fs::weakly_canonical(fs::absolute(a)) == fs::weakly_canonical(fs::absolute(b)) ||
               (fs::exists(a) && fs::exists(b) && fs::equivalent(a, b));
    };
    for (const auto &target : {destination, png_destination, obj_destination, raw_destination}) {
        if (target.empty())
            continue;
        for (const auto &directory : protected_directories) {
            auto base = fs::weakly_canonical(fs::absolute(directory));
            auto resolved = fs::weakly_canonical(fs::absolute(target));
            auto relative = resolved.lexically_relative(base);
            bool within = resolved == base || (!relative.empty() && *relative.begin() != "..");
            for (auto parent = resolved; !within && !parent.empty();) {
                if (fs::exists(parent) && fs::exists(base) && fs::equivalent(parent, base))
                    within = true;
                auto next = parent.parent_path();
                if (next == parent)
                    break;
                parent = next;
            }
            if (within)
                throw Error("output-path",
                            "Output must not overwrite protected content, Git metadata or LFS storage");
        }
        for (const auto &input : protected_files)
            if (same_file(input, target))
                throw Error("output-path",
                            "Output must not overwrite an input package, companion or project descriptor");
    }
    if (!destination.empty() && !png_destination.empty() && same_file(destination, png_destination))
        throw Error("output-path", "Report and image must have distinct destinations");
    if (!raw_destination.empty()) {
        std::ofstream stream(raw_destination, std::ios::binary);
        if (!stream)
            throw Error("io", "Cannot create member output");
        stream.write(reinterpret_cast<const char *>(raw_bytes.data()),
                     static_cast<std::streamsize>(raw_bytes.size()));
        if (!stream)
            throw Error("io", "Cannot write member output");
    }
    if (!obj_destination.empty())
        write_obj(obj_destination, obj);
    if (!png_destination.empty())
        write_png(png_destination, png);
    if (destination.empty())
        std::cout << text;
    else {
        std::ofstream stream(destination, std::ios::binary);
        if (!stream)
            throw Error("io", "Cannot create output file");
        stream << text;
        if (!stream)
            throw Error("io", "Cannot write output file");
    }
    return exit_code;
}
int checked_run(const std::vector<std::string> &args) {
    try {
        return run(args);
    } catch (const Error &e) {
        std::cerr << Json{{"error", e.code}, {"message", e.what()}, {"offset", e.offset}}.dump() << '\n';
        return 2;
    } catch (const std::exception &e) {
        std::cerr << Json{{"error", "exception"}, {"message", e.what()}}.dump() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(fs::path(argv[i]).u8string());
    return checked_run(args);
}
#else
int main(int argc, char **argv) {
    return checked_run({argv + 1, argv + argc});
}
#endif
