#include "json_output.hpp"
namespace cyuediff::detail {
JsonOutput::JsonOutput(std::ostream &out, int indent, const OperationControl &control)
    : out_(out), step_(indent), control_(control),
      serializer_(nlohmann::detail::output_adapter<char>(out), ' ', Json::error_handler_t::replace) {
    if (indent < -1 || indent > 16)
        throw Error("report-format", "JSON indentation must be -1 or between 0 and 16");
    check();
}
void JsonOutput::check() const {
    control_.check();
    if (!out_)
        throw Error("report-output", "Cannot write JSON report");
}
void JsonOutput::character(char c) { out_.put(c); }
void JsonOutput::line(unsigned depth) {
    if (depth > 256)
        throw Error("report-depth", "Report nesting exceeds its budget");
    if (pretty()) {
        character('\n');
        for (unsigned i = 0; i < depth * static_cast<unsigned>(step_); ++i)
            character(' ');
    }
}
void JsonOutput::value(const Json &v, unsigned depth) {
    check();
    if (depth > 256)
        throw Error("report-depth", "Report nesting exceeds its budget");
    serializer_.dump(v, pretty(), false, pretty() ? static_cast<unsigned>(step_) : 0,
                     pretty() ? depth * static_cast<unsigned>(step_) : 0);
    check();
}
void JsonOutput::array(std::size_t n, const std::function<void(std::size_t, unsigned)> &emit,
                       unsigned depth) {
    check();
    character('[');
    for (std::size_t i = 0; i < n; ++i) {
        check();
        if (i)
            character(',');
        line(depth + 1);
        emit(i, depth + 1);
    }
    if (n)
        line(depth);
    character(']');
    check();
}
void JsonObject::add(std::string key, Field value) {
    if (!fields_.emplace(std::move(key), std::move(value)).second)
        throw Error("report-field", "Duplicate JSON report field");
}
void JsonObject::borrow(std::string key, const Json &value) {
    const auto *p = &value;
    add(std::move(key), {[p](JsonOutput &out, unsigned depth) { out.value(*p, depth); }, [p] { return *p; }});
}
void JsonObject::own(std::string key, Json value) {
    auto p = std::make_shared<const Json>(std::move(value));
    add(std::move(key), {[p](JsonOutput &out, unsigned depth) { out.value(*p, depth); }, [p] { return *p; }});
}
void JsonObject::produce(std::string key, std::function<Json()> value) {
    auto p = std::make_shared<const std::function<Json()>>(std::move(value));
    add(std::move(key), {[p](JsonOutput &out, unsigned depth) {
                             const auto value = (*p)();
                             out.value(value, depth);
                         },
                         [p] { return (*p)(); }});
}
void JsonObject::object(std::string key, std::function<JsonObject()> value) {
    auto p = std::make_shared<const std::function<JsonObject()>>(std::move(value));
    add(std::move(key), {[p](JsonOutput &out, unsigned depth) { (*p)().write(out, depth); },
                         [p] { return (*p)().to_json(); }});
}
void JsonObject::array(std::string key, std::size_t n, std::function<Json(std::size_t)> item) {
    auto p = std::make_shared<const std::function<Json(std::size_t)>>(std::move(item));
    add(std::move(key), {[p, n](JsonOutput &out, unsigned depth) {
                             out.array(
                                 n,
                                 [&](std::size_t i, unsigned d) {
                                     const auto value = (*p)(i);
                                     out.value(value, d);
                                 },
                                 depth);
                         },
                         [p, n] {
                             Json result = Json::array();
                             for (std::size_t i = 0; i < n; ++i)
                                 result.push_back((*p)(i));
                             return result;
                         }});
}
void JsonObject::objects(std::string key, std::size_t n, std::function<JsonObject(std::size_t)> item) {
    auto p = std::make_shared<const std::function<JsonObject(std::size_t)>>(std::move(item));
    add(std::move(key), {[p, n](JsonOutput &out, unsigned depth) {
                             out.array(n, [&](std::size_t i, unsigned d) { (*p)(i).write(out, d); }, depth);
                         },
                         [p, n] {
                             Json result = Json::array();
                             for (std::size_t i = 0; i < n; ++i)
                                 result.push_back((*p)(i).to_json());
                             return result;
                         }});
}
Json JsonObject::to_json() const {
    Json result = Json::object();
    for (const auto &field : fields_)
        result[field.first] = field.second.materialize();
    return result;
}
void JsonObject::write(JsonOutput &out, unsigned depth) const {
    out.check();
    out.character('{');
    std::size_t n = 0;
    for (const auto &field : fields_) {
        out.check();
        if (n++)
            out.character(',');
        out.line(depth + 1);
        out.value(Json(field.first), depth + 1);
        out.character(':');
        if (out.pretty())
            out.character(' ');
        field.second.emit(out, depth + 1);
    }
    if (n)
        out.line(depth);
    out.character('}');
    out.check();
}
void JsonObject::write(std::ostream &stream, int indent, const OperationControl &control) const {
    JsonOutput output(stream, indent, control);
    write(output);
}
} // namespace cyuediff::detail
