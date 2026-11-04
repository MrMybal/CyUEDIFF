#include "document.hpp"
namespace cyuediff::desktop {
std::string Document::status() const {
#ifdef CYUEDIFF_HAS_GIT
    if (git)
        return git->status();
#endif
    if (diff)
        return diff->status();
    return before && before->complete_input ? "ready" : "incomplete";
}
Json Document::report() const {
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
    auto mesh = [](const std::shared_ptr<const MeshGeometry> &m) -> Json {
        return m ? Json{{"vertices", m->vertices.size()}, {"triangles", m->triangles.size()}} : Json(nullptr);
    };
    Json result = {{"status", status()},
                   {"before", package(before)},
                   {"after", package(after)},
                   {"project", project ? project->to_json() : Json(nullptr)},
                   {"diff", diff ? diff->to_json() : Json(nullptr)},
                   {"previews", images},
                   {"beforeMesh", mesh(before_mesh)},
                   {"afterMesh", mesh(after_mesh)},
                   {"diagnostics", diagnostics}};
#ifdef CYUEDIFF_HAS_GIT
    result["git"] = git ? git->to_json() : Json(nullptr);
#else
    result["git"] = nullptr;
#endif
    return result;
}
std::shared_ptr<const Document> load_document(const LoadRequest &request) {
    if (request.before.empty())
        throw Error("desktop-input", "Choose a package before loading");
    auto document = std::make_shared<Document>();
    document->request = request;
    if (request.git) {
#ifdef CYUEDIFF_HAS_GIT
        const auto &source = *request.git;
        if (source.repository.empty() || source.before_revision.empty() || source.after_revision.empty())
            throw Error("desktop-git", "Choose a repository and two Git revisions");
        GitOptions options;
        options.lfs_objects = source.lfs_objects;
        auto repository = GitRepository::open(source.repository, options);
        document->protected_files.push_back(repository.root() / ".git");
        document->protected_directories = {repository.common_directory(), repository.lfs_objects()};
        GitComparison comparison;
        if (!request.project.empty()) {
            GitProjectOptions context_options;
            context_options.engine_root = request.engine_root;
            context_options.discover_engine = source.discover_engine;
            if (!source.discover_engine && !request.engine_root.empty())
                throw Error("desktop-git", "Engine discovery disabled with an explicit engine root");
            auto a = repository.project(source.before_revision, request.project.generic_u8string(),
                                        context_options);
            auto b = repository.project(source.after_revision, request.project.generic_u8string(),
                                        context_options);
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
            comparison = compare_git(repository, a, b, request.before, request.after);
        } else {
            if (!request.engine_root.empty() || !source.discover_engine)
                throw Error("desktop-git", "Engine selection requires a historical project descriptor");
            comparison = compare_git(repository, source.before_revision, source.after_revision,
                                     request.before, request.after);
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
        auto resolve = [&](const std::string &name) {
            if (document->project && name.rfind("/", 0) == 0)
                return document->project->resolve(name);
            return fs::u8path(name);
        };
        document->before = std::make_shared<const Package>(Package::open(resolve(request.before)));
        if (!request.after.empty()) {
            document->after = std::make_shared<const Package>(Package::open(resolve(request.after)));
            document->diff = compare(*document->before, *document->after);
        }
    }
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
                options.decoder = decoder;
                options.max_pixels = 16ull * 1024 * 1024;
                options.max_payload_bytes = 128ull * 1024 * 1024;
                options.max_decoder_bytes = 128ull * 1024 * 1024;
                document->previews.push_back({package->textures.front().object_path, after,
                                              decode_texture(package->textures.front(), 0, options)});
            } catch (const std::exception &e) {
                document->diagnostics.push_back(side + std::string(e.what()));
            }
        }
        if (!package->mesh_sources.empty()) {
            try {
                MeshDecodeOptions options;
                options.decoder = decoder;
                options.max_payload_bytes = 128ull * 1024 * 1024;
                auto mesh =
                    std::make_shared<const MeshGeometry>(decode_mesh(package->mesh_source(), options));
                (after ? document->after_mesh : document->before_mesh) = std::move(mesh);
            } catch (const std::exception &e) {
                document->diagnostics.push_back(side + std::string(e.what()));
            }
        }
    };
    previews(document->before, false);
    previews(document->after, true);
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
        pending_.reset();
    }
    wake_.notify_one();
    worker_.join();
}
void DocumentSession::request(LoadRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = Work{++generation_, std::move(request)};
        result_.reset();
    }
    busy_ = true;
    error_.clear();
    wake_.notify_one();
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
