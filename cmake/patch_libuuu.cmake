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

file(WRITE "${cml}" "${t}")

# Remove std::mutex from the USB device filter struct in usbhotplug.cpp.
#
# uuu_add_usbserial_no_filter / uuu_add_usbpath_filter crash on Windows 10
# inside lock_guard<mutex> — suspected MSVC static-lib mutex-init issue.
# These functions are always called from the main thread BEFORE polling_usb
# starts any threads, so the mutex provides no actual safety benefit.
# is_valid() is called from the polling thread, but only after push_back()
# has already returned, so removing the lock there is also safe for our
# single-setup / multi-read pattern.
set(hotplug "libuuu/usbhotplug.cpp")
file(READ "${hotplug}" src)

string(REPLACE
  "struct filter {
\tvector<string> list;
\tmutex lock;

\tvoid push_back(string filter)
\t{
\t\tlock_guard<mutex> guard{lock};
\t\tlist.emplace_back(std::move(filter));
\t}
};"
  "struct filter {
\tvector<string> list;

\tvoid push_back(string filter)
\t{
\t\tlist.emplace_back(std::move(filter));
\t}
};"
  src "${src}")

# Also remove the lock_guard calls from is_valid() in both filter instances.
string(REPLACE "lock_guard<mutex> guard{lock};\n\t\tif (list.empty())"
               "if (list.empty())" src "${src}")

file(WRITE "${hotplug}" "${src}")

# Same crash pattern in notify.cpp: static mutex g_mutex_notify.
# Registration/unregistration only happens from the main thread before flash
# threads start, and call_notify reads an immutable list during flash, so the
# mutex provides no real safety benefit for our usage.
set(notifysrc "libuuu/notify.cpp")
file(READ "${notifysrc}" src)
string(REPLACE "static mutex g_mutex_notify;\n" "" src "${src}")
string(REPLACE "\tstd::lock_guard<mutex> lock(g_mutex_notify);\n" "" src "${src}")
file(WRITE "${notifysrc}" "${src}")

# Same crash pattern in error.cpp: static mutex g_last_error_str_mutex.
# Error strings are diagnostic/last-write-wins; a benign race is acceptable.
set(errorsrc "libuuu/error.cpp")
file(READ "${errorsrc}" src)
string(REPLACE "static mutex g_last_error_str_mutex;\n" "" src "${src}")
string(REPLACE "\tlock_guard<mutex> l(g_last_error_str_mutex);\n" "" src "${src}")
file(WRITE "${errorsrc}" "${src}")

# Same crash pattern in buffer.cpp: static mutex g_mutex_map.
# g_mutex_map protects the global buffer cache against concurrent open/close.
# Our flash sequence is sequential (boot then emmc, single device), so no
# concurrent buffer map access occurs in practice.
set(buffersrc "libuuu/buffer.cpp")
file(READ "${buffersrc}" src)
string(REPLACE "static mutex g_mutex_map;\n" "" src "${src}")
string(REGEX REPLACE "\t+std::lock_guard<mutex> lock\\(g_mutex_map\\);\n" "" src "${src}")
file(WRITE "${buffersrc}" "${src}")
