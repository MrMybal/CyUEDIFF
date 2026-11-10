#pragma once
#include <cyuediff/defaults.hpp>
namespace cyuediff::detail {
Json validate_blueprint_default_binding(const DefaultSnapshot &, const Json &source, const Json &environment,
                                        const OperationControl &);
}
