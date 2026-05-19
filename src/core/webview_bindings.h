#pragma once

#include <string>

namespace bindings {

bool parse_bool_arg(const std::string& req, bool fallback);
std::string parse_string_arg(const std::string& req);
std::string js_string_literal(const std::string& input);

} // namespace bindings
