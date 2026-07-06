#pragma once

#include <optional>
#include <string>

std::optional<std::string> pick_img_file(std::string& error_message);
std::optional<std::string> pick_img_save_file(std::string& error_message);
