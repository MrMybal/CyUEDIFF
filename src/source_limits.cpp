#include <cyuediff/source_limits.hpp>
namespace cyuediff {
void SourceReadLimits::validate() const {
    if (!max_properties || max_properties > maximum_work || !max_property_bytes ||
        max_property_bytes > maximum_bytes)
        throw Error("source-read-limits",
                    "Source limits require 1..4000000 work units and 1..268435456 bytes per property");
}
bool SourceReadLimits::is_default() const {
    const SourceReadLimits defaults;
    return max_properties == defaults.max_properties && max_property_bytes == defaults.max_property_bytes;
}
void SourceReadLimits::apply(ReadOptions &options) const {
    validate();
    options.max_properties = max_properties;
    options.max_property_bytes = max_property_bytes;
}
Json SourceReadLimits::to_json() const {
    validate();
    return {{"maxProperties", max_properties}, {"maxPropertyBytes", max_property_bytes}};
}
} // namespace cyuediff
