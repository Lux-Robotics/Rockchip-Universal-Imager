#include "core/file_drop.h"

namespace rui {

void install_file_drop_target(const std::shared_ptr<saucer::window>& /*window*/,
                              std::function<void(const std::string&)> /*on_drop*/) {
    // No host-level .img path drop yet; users pick files via the dialog.
}

} // namespace rui
