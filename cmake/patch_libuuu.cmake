# Make libuuu's gitversion.h generation cross-platform.
#
# Upstream generates the header with POSIX shell commands:
#     COMMAND mkdir -p ${generated_files_dir}
#     COMMAND sh -c '... ./gen_ver.sh ...'
# which fail on Windows ("The syntax of the command is incorrect"). The header
# only needs to define GIT_VERSION, so we drop a static one and rewrite the
# command to a portable `cmake -E copy`.
#
# Run as a FetchContent PATCH_COMMAND with the working directory at the
# mfgtools source root. Idempotent: re-running finds nothing to replace.

file(WRITE "libuuu/gitversion_static.h" "#define GIT_VERSION \"libuuu_1.5.243\"\n")

set(cml "libuuu/CMakeLists.txt")
file(READ "${cml}" t)

string(REPLACE
  "COMMAND mkdir -p \${generated_files_dir}"
  "COMMAND \${CMAKE_COMMAND} -E make_directory \${generated_files_dir}"
  t "${t}")

string(REPLACE
  "COMMAND sh -c 'cd \${CMAKE_CURRENT_SOURCE_DIR} && rm -f \${gitversion_h} && ./gen_ver.sh \"\${gitversion_h}.tmp\" && mv -f \"\${gitversion_h}.tmp\" \"\${gitversion_h}\"'"
  "COMMAND \${CMAKE_COMMAND} -E copy \${CMAKE_CURRENT_SOURCE_DIR}/gitversion_static.h \${gitversion_h}"
  t "${t}")

# On Windows we supply libusb from the bundled submodule (same version as the
# official NXP uuu binary). Skip pkg_check_modules when the parent has already
# set LIBUSB_INCLUDE_DIRS so that the include_directories() call below still
# picks up the right path without requiring a system libusb pkg-config entry.
string(REPLACE
  "pkg_check_modules(LIBUSB REQUIRED libusb-1.0>=1.0.16)"
  "if(NOT LIBUSB_INCLUDE_DIRS)\n  pkg_check_modules(LIBUSB REQUIRED libusb-1.0>=1.0.16)\nendif()"
  t "${t}")

file(WRITE "${cml}" "${t}")
