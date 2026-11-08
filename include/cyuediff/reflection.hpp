#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
// Declared field metadata present in supported serialized UStruct prefixes.
// Does not resolve inherited defaults or execute/derive arbitrary native serializers.
Json reflection_definitions(const Package &, const OperationControl &control = {});
} // namespace cyuediff
