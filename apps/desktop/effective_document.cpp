#include "effective_document.hpp"
#include "../../src/json_output.hpp"
#include <sstream>
namespace cyuediff::desktop {
namespace {
void propagate_cancel(const Error &e) {
    if (e.code == "cancelled")
        throw;
}
// Bound text while streaming, before a large scalar/container can allocate its dump.
class PreviewBuffer : public std::streambuf {
    std::string value_;
    std::streamsize xsputn(const char *p, std::streamsize n) override {
        if (n > 2048 - static_cast<std::streamsize>(value_.size()))
            throw std::length_error("preview");
        value_.append(p, static_cast<std::size_t>(n));
        return n;
    }
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            char v = traits_type::to_char_type(c);
            xsputn(&v, 1);
        }
        return traits_type::not_eof(c);
    }

  public:
    const std::string &value() const { return value_; }
};
std::string preview(const Json &value) {
    PreviewBuffer b;
    std::ostream out(&b);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    try {
        out << value;
        return b.value();
    } catch (const std::length_error &) {
        return "[value exceeds preview limit]";
    }
}
PropertyTable changes(const EffectiveDefaultsComparison &c, const EffectiveDefaults &a,
                      const EffectiveDefaults &b, const OperationControl &control) {
    PropertyTable table;
    auto append = [&](const Json &entry, bool unknown) {
        control.check();
        if (table.rows.size() == 20000) {
            table.truncated = true;
            return;
        }
        PropertyRow row;
        row.path = entry.at("path").get<std::string>();
        row.label = row.path;
        // A short display label keeps the field visible; the original qualified pointer remains the identity.
        std::vector<std::string> tokens;
        std::size_t at = 0;
        while (at < row.path.size()) {
            const auto end = row.path.find('/', at + 1);
            auto raw = row.path.substr(at + 1, end == std::string::npos ? end : end - at - 1);
            std::string token;
            for (std::size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '~' && i + 1 < raw.size()) {
                    token += raw[++i] == '1' ? '/' : '~';
                } else
                    token += raw[i];
            }
            tokens.push_back(std::move(token));
            if (end == std::string::npos)
                break;
            at = end;
        }
        auto short_field = [](const std::string &name) {
            if (name.compare(0, 8, "/Script/") != 0)
                return name;
            const auto dot = name.rfind('.');
            return dot == std::string::npos ? name : name.substr(dot + 1);
        };
        if (tokens.size() >= 2 && tokens[0] == "fields") {
            row.label = short_field(tokens[1]);
            for (std::size_t i = 2; i < tokens.size(); ++i)
                if (tokens[i] != "value" && tokens[i] != "entries")
                    row.label += " / " + short_field(tokens[i]);
        }

        row.type = unknown ? entry.at("reason").get<std::string>() : "effective";
        if (!unknown && tokens.size() >= 2 && tokens[0] == "fields") {
            const auto &fields = a.fields.contains(tokens[1]) ? a.fields : b.fields;
            if (fields.contains(tokens[1]) && fields.at(tokens[1]).contains("type"))
                row.type = fields.at(tokens[1]).at("type").value("name", row.type);
        }
        row.change = unknown                      ? ChangeKind::unknown
                     : entry.at("op") == "add"    ? ChangeKind::added
                     : entry.at("op") == "remove" ? ChangeKind::removed
                                                  : ChangeKind::modified;
        row.before_present = entry.contains("before");
        row.after_present = entry.contains("after");
        row.before_available = !unknown;
        row.after_available = !unknown;
        if (row.before_present)
            row.before = preview(entry.at("before"));
        if (row.after_present)
            row.after = preview(entry.at("after"));
        table.rows.push_back(std::move(row));
    };
    for (const auto &entry : c.changes) {
        append(entry, false);
        if (table.truncated)
            break;
    }
    for (const auto &entry : c.unavailable) {
        append(entry, true);
        if (table.truncated)
            break;
    }
    return table;
}
std::string root(const Package &p, const DefaultContext &ctx, std::string object) {
    if (!object.empty())
        return object;
    const auto roots = default_root_exports(p, ctx);
    if (roots.size() > 1)
        throw Error("default-context", "Choose a root export explicitly");
    if (!roots.empty())
        object = roots.front();
    if (object.empty())
        throw Error("default-context", "No root export matches the prepared class");
    return object;
}
} // namespace
detail::JsonObject EffectiveDefaultsDocument::report_view() const {
    detail::JsonObject out;
    auto effective = [](const EffectiveDefaults &model) {
        detail::JsonObject item;
        for (auto it = model.provenance.begin(); it != model.provenance.end(); ++it)
            item.borrow(it.key(), it.value());
        item.own("schema", "cyuediff.effective-defaults/1");
        item.own("scope", "effective-reflected-properties");
        item.own("complete", model.complete);
        item.borrow("fields", model.fields);
        item.borrow("unavailable", model.unavailable);
        return item;
    };
    if (before)
        out.object("before", [this, effective] { return effective(*before); });
    else
        out.own("before", nullptr);
    if (after)
        out.object("after", [this, effective] { return effective(*after); });
    else
        out.own("after", nullptr);
    if (comparison)
        out.object("comparison", [this] {
            detail::JsonObject item;
            for (auto it = comparison->provenance.begin(); it != comparison->provenance.end(); ++it)
                item.borrow(it.key(), it.value());
            item.own("schema", "cyuediff.effective-default-diff/1");
            item.own("scope", "effective-reflected-properties");
            item.own("status", comparison->status());
            item.own("complete", comparison->unavailable.empty());
            item.borrow("changes", comparison->changes);
            item.borrow("unavailable", comparison->unavailable);
            return item;
        });
    else
        out.own("comparison", nullptr);
    out.borrow("contexts", contexts);
    out.own("beforeError", before_error);
    out.own("afterError", after_error);
    out.own("presentationRows", changes.rows.size());
    out.own("presentationTruncated", changes.truncated);
    return out;
}
Json EffectiveDefaultsDocument::report() const { return report_view().to_json(); }
PreparedDefaultContexts::PreparedDefaultContexts(Document &doc, const LoadRequest &request, ReadOptions &a,
                                                 ReadOptions &b) {
    if (!request.effective_defaults)
        return;
    if (request.container)
        throw Error("desktop-defaults", "Prepared source defaults require loose or Git packages");
    result_ = std::make_shared<EffectiveDefaultsDocument>();
    const auto &selected = *request.effective_defaults;
    auto load = [&](const fs::path &file, ReadOptions &read,
                    std::string &error) -> std::optional<DefaultContext> {
        if (file.empty()) {
            error = "No prepared default context selected";
            return {};
        }
        doc.protected_files.push_back(file);
        try {
            DefaultContextReadOptions options;
            options.document.control = request.control;
            options.snapshot.capture.control = request.control;
            options.snapshot.values.control = request.control;
            request.control.checkpoint("Verifying prepared default context");
            auto context = DefaultContext::read(file, options);
            doc.protected_files.insert(doc.protected_files.end(), context.input_files().begin(),
                                       context.input_files().end());
            read.value_identities.names = true;
            read.value_identities.max_names = read.max_properties;
            read.value_identities.max_text_bytes = read.max_property_bytes;
            return context;
        } catch (const Error &e) {
            propagate_cancel(e);
            error = e.what();
            return {};
        }
    };
    before_ = load(selected.before_context, a, result_->before_error);
    const auto after_path = selected.inherit_after_context ? selected.before_context : selected.after_context;
    const bool paired = request.git.has_value() || !request.after.empty();
    if (paired) {
        if (before_ && !after_path.empty() &&
            fs::absolute(after_path).lexically_normal() ==
                fs::absolute(selected.before_context).lexically_normal()) {
            after_ = before_;
            b.value_identities = a.value_identities;
            // Independent source read limits still bound the optional component capture.
            b.value_identities.max_names = b.max_properties;
            b.value_identities.max_text_bytes = b.max_property_bytes;
        } else
            after_ = load(after_path, b, result_->after_error);
    }
    result_->contexts = {{"before", before_ ? before_->provenance() : Json(nullptr)},
                         {"after", after_ ? after_->provenance() : Json(nullptr)},
                         {"inheritAfter", selected.inherit_after_context}};
}
void PreparedDefaultContexts::resolve(Document &doc, const LoadRequest &request) {
    if (!result_)
        return;
    const auto &selected = *request.effective_defaults;
    EffectiveDefaultsOptions options;
    options.control = request.control;
    auto resolve = [&](const std::shared_ptr<const Package> &p, const std::optional<DefaultContext> &ctx,
                       const std::string &object,
                       std::string &error) -> std::shared_ptr<const EffectiveDefaults> {
        if (!ctx)
            return {};
        if (!p) {
            error = "Source package is absent; effective defaults cannot infer an object addition/removal";
            return {};
        }
        try {
            request.control.checkpoint("Resolving effective source properties");
            return std::make_shared<const EffectiveDefaults>(
                resolve_source_defaults(*p, root(*p, *ctx, object), *ctx, options));
        } catch (const Error &e) {
            propagate_cancel(e);
            error = e.what();
            return {};
        }
    };
    result_->before = resolve(doc.before, before_, selected.before_object, result_->before_error);
    if (doc.after || request.git || !request.after.empty())
        result_->after = resolve(doc.after, after_, selected.after_object, result_->after_error);
    if (result_->before && result_->after) {
        result_->comparison = std::make_shared<const EffectiveDefaultsComparison>(
            compare_effective_defaults(*result_->before, *result_->after, options));
        result_->changes = changes(*result_->comparison, *result_->before, *result_->after, request.control);
    }
    doc.effective_defaults = std::move(result_);
}
} // namespace cyuediff::desktop
