#pragma once
#include <cyuediff/core.hpp>
#include <ostream>
namespace cyuediff {
struct GitComparison;
}
namespace cyuediff::detail {
class JsonOutput;
// Synchronous report view. Borrowed fields must outlive writing/materialization.
class JsonObject {
    struct Field {
        std::function<void(JsonOutput &, unsigned)> emit;
        std::function<Json()> materialize;
    };
    std::map<std::string, Field> fields_;
    void add(std::string, Field);

  public:
    void borrow(std::string, const Json &);
    void own(std::string, Json);
    void produce(std::string, std::function<Json()>);
    void object(std::string, std::function<JsonObject()>);
    void array(std::string, std::size_t, std::function<Json(std::size_t)>);
    void objects(std::string, std::size_t, std::function<JsonObject(std::size_t)>);
    Json to_json() const;
    void write(JsonOutput &, unsigned depth = 0) const;
    void write(std::ostream &, int indent = 2, const OperationControl & = {}) const;
};
class JsonOutput {
    std::ostream &out_;
    int step_;
    OperationControl control_;
    nlohmann::detail::serializer<Json> serializer_;

  public:
    JsonOutput(std::ostream &, int, const OperationControl & = {});
    void check() const;
    void value(const Json &, unsigned depth = 0);
    void character(char);
    void line(unsigned);
    bool pretty() const { return step_ >= 0; }
    void array(std::size_t, const std::function<void(std::size_t, unsigned)> &, unsigned);
};
JsonObject diff_report(const DiffResult &);
JsonObject git_report(const GitComparison &);
} // namespace cyuediff::detail
