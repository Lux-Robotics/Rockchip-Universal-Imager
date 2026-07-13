# Build-time patch for rkdeveloptool: make stdout unbuffered.
#
# Upstream prints flash progress with printf and never calls fflush. That's
# fine on a terminal (line-buffered), but the app runs it through a pipe,
# where stdio switches to full (4KB block) buffering - progress output then
# arrives in rare multi-kilobyte bursts (or only at process exit), which is
# why the app's progress bar and log appeared stuck at 0% during a flash.
#
# Idempotent: skipped if the setvbuf call is already present.

if(NOT DEFINED RKDEV_MAIN OR NOT EXISTS "${RKDEV_MAIN}")
    message(FATAL_ERROR "rkdev_patch: RKDEV_MAIN not set or missing: ${RKDEV_MAIN}")
endif()

file(READ "${RKDEV_MAIN}" content)

if(content MATCHES "setvbuf")
    return()
endif()

set(needle "int main(int argc, char* argv[])\n{")
set(replacement "int main(int argc, char* argv[])\n{\n\tsetvbuf(stdout, NULL, _IONBF, 0);")

string(FIND "${content}" "${needle}" needle_idx)
if(needle_idx EQUAL -1)
    message(WARNING "rkdev_patch: main() signature not found in ${RKDEV_MAIN} - "
                    "stdout stays block-buffered and flash progress will lag")
    return()
endif()

string(REPLACE "${needle}" "${replacement}" content "${content}")
file(WRITE "${RKDEV_MAIN}" "${content}")
message(STATUS "rkdev_patch: made rkdeveloptool stdout unbuffered")
