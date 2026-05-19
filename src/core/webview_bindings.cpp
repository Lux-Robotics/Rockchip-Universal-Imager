#include "core/webview_bindings.h"

#include <cctype>

namespace bindings {
namespace {

std::string trim_copy(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(start, end - start);
}

std::string strip_json_array(const std::string& input) {
    std::string trimmed = trim_copy(input);
    if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
        trimmed = trim_copy(trimmed.substr(1, trimmed.size() - 2));
    }
    return trimmed;
}

std::string unescape_json_string(const std::string& input) {
    if (input.size() < 2 || input.front() != '"' || input.back() != '"') {
        return input;
    }
    std::string out;
    out.reserve(input.size() - 2);
    for (size_t i = 1; i + 1 < input.size(); ++i) {
        char ch = input[i];
        if (ch == '\\' && i + 1 < input.size() - 1) {
            char esc = input[++i];
            switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            default: out.push_back(esc); break;
            }
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

} // namespace

bool parse_bool_arg(const std::string& req, bool fallback) {
    const std::string token = strip_json_array(req);
    if (token == "true") {
        return true;
    }
    if (token == "false") {
        return false;
    }
    return fallback;
}

std::string parse_string_arg(const std::string& req) {
    const std::string token = strip_json_array(req);
    return unescape_json_string(token);
}

std::string js_string_literal(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 2);
    out.push_back('"');
    for (char ch : input) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    out.push_back('"');
    return out;
}

} // namespace bindings
