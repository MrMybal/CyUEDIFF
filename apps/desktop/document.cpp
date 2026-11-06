#include <cyuediff/mesh_skin.hpp>
#include <cyuediff/mesh_morphs.hpp>
#include "document.hpp"
#include "animation_document.hpp"
#include <cyuediff/binary_layout.hpp>
namespace cyuediff::desktop {
std::vector<TextureDifference> compare_previews(const std::vector<Preview> &previews) {
    std::vector<const Preview *> a, b;
    for (const auto &preview : previews)
        (preview.after ? b : a).push_back(&preview);
    std::vector<TextureDifference> result;
    for (auto before : a) {
        const Preview *after = nullptr;
        for (auto candidate : b)
            if (candidate->object_path == before->object_path) {
                after = candidate;
                break;
            }
        if (!after && a.size() == 1 && b.size() == 1)
            after = b.front();
        if (!after)
            continue;
        bool same = before->image.width == after->image.width && before->image.height == after->image.height;
        TextureDifference difference;
        difference.before_object = before->object_path;
        difference.after_object = after->object_path;
        if (before->hdr && after->hdr) {
            auto hdr = compare_hdr_images(*before->hdr, *after->hdr, 0, same);
            difference.comparison = {hdr.comparable,
                                     hdr.same_size,
                                     hdr.before_width,
                                     hdr.before_height,
                                     hdr.after_width,
                                     hdr.after_height,
                                     hdr.changed_pixels,
                                     0,
                                     0,
                                     0,
                                     std::move(hdr.heatmap)};
            difference.hdr_comparison = std::move(hdr);
        } else if (bool(before->hdr) != bool(after->hdr)) {
            difference.comparison = compare_images(before->image, after->image, 0, false);
            difference.comparison.comparable = false;
            difference.diagnostic = "HDR/LDR sample domains differ";
        } else
            difference.comparison = compare_images(before->image, after->image, 0, same);
        result.push_back(std::move(difference));
    }
    return result;
}
std::string Document::status() const {
    if (container)
        return "ready";
#ifdef CYUEDIFF_HAS_GIT
    if (git)
        return git->status();
#endif
    if (diff)
        return diff->status();
    return before && before->complete_input ? "ready" : "incomplete";
}
Json Document::report() const {
    if (container)
        return {{"status", status()}, {"container", container->report()}};
    auto package = [](const std::shared_ptr<const Package> &p) -> Json {
        if (!p)
            return nullptr;
        return {{"source", p->source.u8string()},
                {"exports", p->exports.size()},
                {"completeInput", p->complete_input},
                {"diagnostics", p->diagnostics}};
    };
    Json images = Json::array();
    for (const auto &preview : previews)
        images.push_back({{"object", preview.object_path},
                          {"after", preview.after},
                          {"width", preview.image.width},
                          {"height", preview.image.height}});
    Json differences = Json::array();
    for (const auto &d : texture_differences)
        differences.push_back(
            {{"before", d.before_object},
             {"after", d.after_object},
             {"comparison", d.hdr_comparison ? d.hdr_comparison->to_json() : d.comparison.to_json()},
             {"diagnostic", d.diagnostic}});
    auto mesh = [](const std::shared_ptr<const MeshGeometry> &m) -> Json {
        if (!m) return Json(nullptr);
        Json result = {{"vertices", m->vertices.size()}, {"triangles", m->triangles.size()}};
        if (m->skin) {
            result["sourceBones"] = m->skin->bones.size();
            result["skinProfiles"] = m->skin->profiles.size();
        }
        if (m->morphs)
            result["morphTargets"] = m->morphs->targets.size();
        return result;
    };
    Json result = {{"status", status()},
                   {"before", package(before)},
                   {"after", package(after)},
                   {"project", project ? project->to_json() : Json(nullptr)},
                   {"afterProject", after_project ? after_project->to_json() : Json(nullptr)},
                   {"diff", diff ? diff->to_json() : Json(nullptr)},
                   {"previews", images},
                   {"textureDifferences", differences},
                   {"beforeMesh", mesh(before_mesh)},
                   {"afterMesh", mesh(after_mesh)},
                   {"diagnostics", diagnostics},
                   {"animationPose", animation_pose_report(*this)}};
    if (!layout_contexts.is_null()) result["layoutContexts"] = layout_contexts;
#ifdef CYUEDIFF_HAS_GIT
    result["git"] = git ? git->to_json() : Json(nullptr);
#else
    result["git"] = nullptr;
#endif
    return result;
}
std::shared_ptr<const Document> load_document(const LoadRequest &request) {
    if (request.before.empty() && !request.container)
        throw Error("desktop-input", "Choose a package before loading");
    if (request.animation_pose && request.container)
        throw Error("desktop-animation-pose", "External Skeleton bindings require loose or Git packages");
    request.control.checkpoint("Opening document");
    ReadOptions read;
    read.control = request.control;
    auto document = std::make_shared<Document>();
    document->request = request;
    document->request.cache.reset();
    document->request.control = {}; // Do not retain session callbacks in a completed document.
    if (request.container) {
        if (!request.layouts.empty() || !request.after_layouts.empty() || !request.inherit_after_layouts)
            throw Error("desktop-layouts", "Cooked structures use inline schema context layouts");
        if (request.git || !request.before.empty() || !request.after.empty())
            throw Error("desktop-container", "Container requests cannot include loose or Git packages");
        document->container = load_container_document(*request.container, request.codec, request.control);
        document->protected_files = document->container->protected_files;
        return document;
    }
    for (const auto &root : {request.payload_root, request.after_payload_root})
        if (!root.empty()) document->protected_directories.push_back(root);
    auto load_layouts = [&](const fs::path &path) -> std::shared_ptr<const BinaryLayouts> {
        if (path.empty()) return {};
        BinaryLayoutOptions options;
        options.control = request.control;
        document->protected_files.push_back(path);
        return std::make_shared<const BinaryLayouts>(BinaryLayouts::read(path, options));
    };
    const auto before_layouts = load_layouts(request.layouts);
    const auto after_layouts = !request.after_layouts.empty() ? load_layouts(request.after_layouts) :
        (request.inherit_after_layouts ? before_layouts : nullptr);
    BinaryLayoutReadOptions layout_options;
    layout_options.control = request.control;
    if (before_layouts) read.decoders = before_layouts->decoder_registry(layout_options);
    read.narrow_string_encoding = before_layouts ? before_layouts->narrow_string_encoding() : NarrowStringEncoding::unspecified;
    auto after_read = read;
    after_read.narrow_string_encoding = after_layouts ? after_layouts->narrow_string_encoding() : NarrowStringEncoding::unspecified;
    after_read.decoders = after_layouts == before_layouts ? read.decoders :
        (after_layouts ? after_layouts->decoder_registry(layout_options) : nullptr);
    if (before_layouts || after_layouts) {
        document->layout_contexts = {{"beforeSha256", before_layouts ? before_layouts->sha256() : std::string{}},
                                     {"afterSha256", after_layouts ? after_layouts->sha256() : std::string{}},
                                     {"contextChanged", before_layouts != after_layouts &&
                                         (!before_layouts || !after_layouts || before_layouts->sha256() != after_layouts->sha256())}};
    }
    if (request.git) {
#ifdef CYUEDIFF_HAS_GIT
        const auto &source = *request.git;
        if (source.repository.empty() || source.before_revision.empty() || source.after_revision.empty())
            throw Error("desktop-git", "Choose a repository and two Git revisions");
        GitOptions options;
        options.control = request.control;
        options.lfs_objects = source.lfs_objects;
        auto repository = GitRepository::open(source.repository, options);
        document->protected_files.push_back(repository.root() / ".git");
        document->protected_directories = {repository.common_directory(), repository.lfs_objects()};
        GitComparison comparison;
        if (request.project.empty() && (!request.after_project.empty() || !request.after_engine_root.empty()))
            throw Error("desktop-git", "Independent Git contexts require a historical project on both sides");
        if (!request.project.empty()) {
            GitProjectOptions context_options;
            context_options.engine_root = request.engine_root;
            context_options.discover_engine = source.discover_engine;
            if (!source.discover_engine && !request.engine_root.empty())
                throw Error("desktop-git", "Engine discovery disabled with an explicit engine root");
            auto a = repository.project(source.before_revision, request.project.generic_u8string(),
                                        context_options);
            auto after_options = context_options;
            if (!request.after_engine_root.empty())
                after_options.engine_root = request.after_engine_root;
            auto b = repository.project(
                source.after_revision,
                (request.after_project.empty() ? request.project : request.after_project).generic_u8string(),
                after_options);
            auto protect_context = [&](const GitProjectContext &context) {
                for (const auto &entry : context.documents)
                    document->protected_files.push_back(
                        entry.file.blob_oid.empty() ? fs::u8path(entry.file.path)
                                                    : repository.root() / fs::u8path(entry.file.path));
                if (!context.engine.root.empty())
                    document->protected_directories.push_back(context.engine.root / "Engine");
                for (const auto &message : context.diagnostics)
                    document->diagnostics.push_back(message);
            };
            protect_context(a);
            protect_context(b);
            document->before_git_project = a;
            document->after_git_project = b;
            comparison = compare_git(repository, a, b, request.before, request.after, read, after_read);
        } else {
            if (!request.engine_root.empty() || !source.discover_engine)
                throw Error("desktop-git", "Engine selection requires a historical project descriptor");
            comparison = compare_git(repository, source.before_revision, source.after_revision,
                                     request.before, request.after, read, after_read);
        }
        auto protect_revision = [&](const GitRevision &revision) {
            auto asset = revision.storage == "git" ? repository.root() / fs::u8path(revision.repository_path)
                                                   : revision.asset_path();
            document->protected_files.push_back(asset);
            for (const auto *extension : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
                auto companion = asset;
                companion.replace_extension(extension);
                document->protected_files.push_back(companion);
            }
        };
        protect_revision(comparison.before);
        protect_revision(comparison.after);
        document->before = comparison.before_package;
        document->after = comparison.after_package;
        document->diff = comparison.diff;
        document->git = std::make_shared<const GitComparison>(std::move(comparison));
#else
        throw Error("desktop-git", "This desktop build does not include the Git adapter");
#endif
    } else {
        if (!request.project.empty())
            document->project = ProjectContext::open(request.project, request.engine_root);
        else if (!request.engine_root.empty())
            throw Error("desktop-project", "An engine root requires a project descriptor");
        auto after_project = request.after_project.empty() ? request.project : request.after_project;
        if (!request.after_project.empty() || !request.after_engine_root.empty()) {
            if (after_project.empty())
                throw Error("desktop-project", "An after-side engine requires a project descriptor");
            document->after_project = ProjectContext::open(after_project, request.after_engine_root.empty()
                                                                              ? request.engine_root
                                                                              : request.after_engine_root);
        }
        auto resolve = [&](const std::string &name, bool after = false) {
            const auto &context =
                after && document->after_project ? document->after_project : document->project;
            if (context && name.rfind("/", 0) == 0)
                return context->resolve(name);
            return fs::u8path(name);
        };
        auto open = [&](const fs::path &path, const ReadOptions &options) {
            return request.cache ? request.cache->open(path, options)
                                 : std::make_shared<const Package>(Package::open(path, options));
        };
        document->before = open(resolve(request.before), read);
        if (!request.after.empty()) {
            document->after = open(resolve(request.after, true), after_read);
            document->diff = compare(*document->before, *document->after, request.control);
        }
    }
    prepare_animation_poses(*document, request, read, after_read);
    if (!request.previews)
        return document;
    BlockDecoder decoder;
    if (!request.codec.empty()) {
        // Explicit host-supplied codec; never discovered from an asset or engine root.
        decoder = load_block_decoder(request.codec);
    }
    auto previews = [&](const std::shared_ptr<const Package> &package, bool after) {
        if (!package)
            return;
        const auto side = after ? "After: " : "Before: ";
        if (!package->textures.empty()) {
            try {
                TextureDecodeOptions options;
                options.virtualized_payload_root = after && !request.inherit_after_payload_root
                    ? request.after_payload_root : request.payload_root;
                options.decoder = decoder;
                options.control = request.control;
                options.max_pixels = 16ull * 1024 * 1024;
                options.max_payload_bytes = 128ull * 1024 * 1024;
                options.max_decoder_bytes = 128ull * 1024 * 1024;
                Preview preview;
                preview.object_path = package->textures.front().object_path;
                preview.after = after;
                if (is_hdr_format(package->textures.front().format)) {
                    preview.hdr = decode_texture_hdr(package->textures.front(), 0, options);
                    preview.image = tone_map_hdr(*preview.hdr, 0, request.control);
                } else
                    preview.image = decode_texture(package->textures.front(), 0, options);
                document->previews.push_back(std::move(preview));
            } catch (const std::exception &e) {
                if (const auto *error = dynamic_cast<const Error *>(&e); error && error->code == "cancelled")
                    throw;
                document->diagnostics.push_back(side + std::string(e.what()));
            }
        }
        if (!package->mesh_sources.empty()) {
            try {
                MeshDecodeOptions options;
                options.virtualized_payload_root = after && !request.inherit_after_payload_root
                    ? request.after_payload_root : request.payload_root;
                options.decoder = decoder;
                options.control = request.control;
                options.max_payload_bytes = 128ull * 1024 * 1024;
                auto mesh =
                    std::make_shared<const MeshGeometry>(decode_mesh(package->mesh_source(), options));
                (after ? document->after_mesh : document->before_mesh) = std::move(mesh);
            } catch (const std::exception &e) {
                if (const auto *error = dynamic_cast<const Error *>(&e); error && error->code == "cancelled")
                    throw;
                document->diagnostics.push_back(side + std::string(e.what()));
            }
        }
    };
    previews(document->before, false);
    previews(document->after, true);
    request.control.checkpoint("Comparing previews");
    document->texture_differences = compare_previews(document->previews);
    request.control.checkpoint("Document ready", 1, 1);
    return document;
}
DocumentSession::DocumentSession(Loader loader) : loader_(std::move(loader)) {
    if (!loader_)
        throw Error("desktop-loader", "Missing document loader");
    worker_ = std::thread([this] { run(); });
}
DocumentSession::~DocumentSession() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (cancel_)
            cancel_->store(true);
        pending_.reset();
    }
    wake_.notify_one();
    worker_.join();
}
void DocumentSession::request(LoadRequest request) {
    if (!request.cache)
        request.cache = cache_;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancel_)
            cancel_->store(true);
        cancel_ = std::make_shared<std::atomic<bool>>(false);
        const auto token = cancel_;
        const auto id = ++generation_;
        auto external = request.control;
        request.control.cancelled = [token, external] {
            return token->load() || (external.cancelled && external.cancelled());
        };
        request.control.progress = [this, id, external](const OperationProgress &value) {
            if (external.progress)
                external.progress(value);
            std::lock_guard<std::mutex> lock(mutex_);
            if (id == generation_)
                progress_ = value;
        };
        pending_ = Work{id, std::move(request)};
        progress_ = {"Opening document", 0, 0};
        result_.reset();
    }
    busy_ = true;
    cancelled_ = false;
    error_.clear();
    wake_.notify_one();
}
void DocumentSession::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancel_)
        cancel_->store(true);
    ++generation_;
    pending_.reset();
    result_.reset();
    busy_ = false;
    cancelled_ = true;
    error_.clear();
    progress_ = {};
}
OperationProgress DocumentSession::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}
bool DocumentSession::poll() {
    std::optional<Result> result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result.swap(result_);
    }
    if (!result || result->id != generation_)
        return false;
    busy_ = false;
    error_ = std::move(result->error);
    if (result->document)
        document_ = std::move(result->document);
    return true;
}
void DocumentSession::run() {
    for (;;) {
        Work work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (stopping_)
                return;
            work = std::move(*pending_);
            pending_.reset();
        }
        Result result{work.id, {}, {}};
        try {
            result.document = loader_(work.request);
            if (!result.document)
                throw Error("desktop-loader", "Loader returned no document");
        } catch (const Error &e) {
            result.error = e.code + ": " + e.what();
        } catch (const std::exception &e) {
            result.error = e.what();
        } catch (...) {
            result.error = "Document loader failed";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
            if (work.id == generation_)
                result_ = std::move(result);
        }
    }
}
} // namespace cyuediff::desktop
