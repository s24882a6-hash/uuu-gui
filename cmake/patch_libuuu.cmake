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

# Add trace points inside uuu_auto_detect_file so we can see exactly where
# the crash is on Windows 10.
set(cmdsrc "libuuu/cmd.cpp")
file(READ "${cmdsrc}" src)
string(REPLACE
  "int uuu_auto_detect_file(const char *filename)
{
\tstring_ex fn;
\tfn += remove_quota(filename);
\tfn.replace('\\\\', '/');

\tif (fn.empty())
\t\tfn += \"./\";

\tstring oldfn =fn;

\tfn += \"/uuu.auto\";
\tshared_ptr<FileBuffer> buffer = get_file_buffer(fn);
\tif (buffer == nullptr)
\t{
\t\tfn.clear();
\t\tfn += oldfn;
\t\tsize_t pos = str_to_upper(fn).find(\"ZIP\");
\t\tif(pos == string::npos || pos != fn.size() - 3)
\t\t{
\t\t\tpos = str_to_upper(fn).find(\"SDCARD\");
\t\t\tif (pos == string::npos || pos != fn.size() - 6)
\t\t\t\tbuffer = get_file_buffer(fn); //we don't try open a zip file here
\t\t}

\t\tif(buffer == nullptr)
\t\t\treturn -1;
\t}

\tstring str= \"uuu_version\";
\tshared_ptr<DataBuffer> pData = buffer->request_data(0, SIZE_MAX);
\tif (!pData)
\t\treturn -1;
\tvoid *p1 = pData->data();
\tvoid *p2 = (void*)str.data();
\tif (memcmp(p1, p2, str.size()) == 0)
\t{
\t\tsize_t pos = fn.rfind('/');
\t\tif (pos != string::npos)
\t\t\tset_current_dir(fn.substr(0, pos + 1));

\t\tg_cmd_list_file = fn.substr(pos+1);

\t\treturn parser_cmd_list_file(pData);
\t}

\t//flash.bin or uboot.bin
\treturn added_default_boot_cmd(fn.c_str());
}"
  "int uuu_auto_detect_file(const char *filename)
{
\tfprintf(stderr, \"[libuuu] auto_detect_file: enter fn=%s\\n\", filename ? filename : \"(null)\"); fflush(stderr);
\tstring_ex fn;
\tfn += remove_quota(filename);
\tfn.replace('\\\\', '/');

\tif (fn.empty())
\t\tfn += \"./\";

\tstring oldfn =fn;

\tfn += \"/uuu.auto\";
\tfprintf(stderr, \"[libuuu] auto_detect_file: get_file_buffer(%s)\\n\", fn.c_str()); fflush(stderr);
\tshared_ptr<FileBuffer> buffer = get_file_buffer(fn);
\tif (buffer == nullptr)
\t{
\t\tfn.clear();
\t\tfn += oldfn;
\t\tsize_t pos = str_to_upper(fn).find(\"ZIP\");
\t\tif(pos == string::npos || pos != fn.size() - 3)
\t\t{
\t\t\tpos = str_to_upper(fn).find(\"SDCARD\");
\t\t\tif (pos == string::npos || pos != fn.size() - 6) {
\t\t\t\tfprintf(stderr, \"[libuuu] auto_detect_file: get_file_buffer(%s)\\n\", fn.c_str()); fflush(stderr);
\t\t\t\tbuffer = get_file_buffer(fn); //we don't try open a zip file here
\t\t\t\tfprintf(stderr, \"[libuuu] auto_detect_file: get_file_buffer done, buffer=%p\\n\", (void*)buffer.get()); fflush(stderr);
\t\t\t}
\t\t}

\t\tif(buffer == nullptr)
\t\t\treturn -1;
\t}

\tfprintf(stderr, \"[libuuu] auto_detect_file: request_data\\n\"); fflush(stderr);
\tstring str= \"uuu_version\";
\tshared_ptr<DataBuffer> pData = buffer->request_data(0, SIZE_MAX);
\tfprintf(stderr, \"[libuuu] auto_detect_file: request_data done, pData=%p\\n\", (void*)pData.get()); fflush(stderr);
\tif (!pData)
\t\treturn -1;
\tvoid *p1 = pData->data();
\tvoid *p2 = (void*)str.data();
\tif (memcmp(p1, p2, str.size()) == 0)
\t{
\t\tsize_t pos = fn.rfind('/');
\t\tif (pos != string::npos)
\t\t\tset_current_dir(fn.substr(0, pos + 1));

\t\tg_cmd_list_file = fn.substr(pos+1);

\t\treturn parser_cmd_list_file(pData);
\t}

\tfprintf(stderr, \"[libuuu] auto_detect_file: added_default_boot_cmd\\n\"); fflush(stderr);
\t//flash.bin or uboot.bin
\tint ret = added_default_boot_cmd(fn.c_str());
\tfprintf(stderr, \"[libuuu] auto_detect_file: done ret=%d\\n\", ret); fflush(stderr);
\treturn ret;
}"
  src "${src}")
file(WRITE "${cmdsrc}" "${src}")

# Add trace points inside FileBuffer::mapfile's Windows branch so we can see
# exactly which WinAPI call crashes on Windows 10 (CreateFile / oplock
# DeviceIoControl / CreateFileMapping / MapViewOfFile).
file(READ "${buffersrc}" src)
string(REPLACE
  "\t\tm_OverLapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
\t\tResetEvent(m_OverLapped.hEvent);

\t\tm_file_handle = CreateFile(filename.c_str(),
\t\t\tGENERIC_READ,
\t\t\tFILE_SHARE_READ | FILE_SHARE_WRITE,
\t\t\tnullptr,
\t\t\tOPEN_EXISTING,
\t\t\tFILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_OVERLAPPED,
\t\t\tnullptr);

\t\tif (m_file_handle == INVALID_HANDLE_VALUE)
\t\t{
\t\t\tstring err = \"Create File Failure \";
\t\t\terr += filename;
\t\t\tset_last_err_string(err);
\t\t\treturn -1;
\t\t}

\t\tBOOL bSuccess = DeviceIoControl(m_file_handle,
\t\t\tFSCTL_REQUEST_OPLOCK,
\t\t\t&m_Request,
\t\t\tsizeof(m_Request),
\t\t\t&Response,
\t\t\tsizeof(Response),
\t\t\tnullptr,
\t\t\t&m_OverLapped);

\t\tif (bSuccess || GetLastError() == ERROR_IO_PENDING)
\t\t{
\t\t\tm_file_monitor = thread(file_overwrite_monitor, filename, this);
\t\t}

\t\tm_file_map = CreateFileMapping(m_file_handle,
\t\t\tnullptr, PAGE_READONLY, 0, 0, nullptr);

\t\tif (m_file_map == INVALID_HANDLE_VALUE)
\t\t{
\t\t\tset_last_err_string(\"Fail create Map\");
\t\t\treturn -1;
\t\t}

\t\tm_pDatabuffer = (uint8_t *)MapViewOfFile(m_file_map, FILE_MAP_READ, 0, 0, sz);
\t\tm_DataSize = sz;
\t\tm_MemSize = sz;
\t\tm_allocate_way = ALLOCATION_WAYS::MMAP;
"
  "\t\tfprintf(stderr, \"[libuuu] mapfile: CreateEvent\\n\"); fflush(stderr);
\t\tm_OverLapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
\t\tResetEvent(m_OverLapped.hEvent);

\t\tfprintf(stderr, \"[libuuu] mapfile: CreateFile(%s) sz=%zu\\n\", filename.c_str(), sz); fflush(stderr);
\t\tm_file_handle = CreateFile(filename.c_str(),
\t\t\tGENERIC_READ,
\t\t\tFILE_SHARE_READ | FILE_SHARE_WRITE,
\t\t\tnullptr,
\t\t\tOPEN_EXISTING,
\t\t\tFILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_OVERLAPPED,
\t\t\tnullptr);
\t\tfprintf(stderr, \"[libuuu] mapfile: CreateFile done handle=%p\\n\", (void*)m_file_handle); fflush(stderr);

\t\tif (m_file_handle == INVALID_HANDLE_VALUE)
\t\t{
\t\t\tstring err = \"Create File Failure \";
\t\t\terr += filename;
\t\t\tset_last_err_string(err);
\t\t\treturn -1;
\t\t}

\t\tfprintf(stderr, \"[libuuu] mapfile: DeviceIoControl FSCTL_REQUEST_OPLOCK\\n\"); fflush(stderr);
\t\tBOOL bSuccess = DeviceIoControl(m_file_handle,
\t\t\tFSCTL_REQUEST_OPLOCK,
\t\t\t&m_Request,
\t\t\tsizeof(m_Request),
\t\t\t&Response,
\t\t\tsizeof(Response),
\t\t\tnullptr,
\t\t\t&m_OverLapped);
\t\tfprintf(stderr, \"[libuuu] mapfile: DeviceIoControl done bSuccess=%d err=%lu\\n\", bSuccess, GetLastError()); fflush(stderr);

\t\tif (bSuccess || GetLastError() == ERROR_IO_PENDING)
\t\t{
\t\t\tfprintf(stderr, \"[libuuu] mapfile: starting file_overwrite_monitor thread\\n\"); fflush(stderr);
\t\t\tm_file_monitor = thread(file_overwrite_monitor, filename, this);
\t\t}

\t\tfprintf(stderr, \"[libuuu] mapfile: CreateFileMapping\\n\"); fflush(stderr);
\t\tm_file_map = CreateFileMapping(m_file_handle,
\t\t\tnullptr, PAGE_READONLY, 0, 0, nullptr);
\t\tfprintf(stderr, \"[libuuu] mapfile: CreateFileMapping done map=%p\\n\", (void*)m_file_map); fflush(stderr);

\t\tif (m_file_map == INVALID_HANDLE_VALUE)
\t\t{
\t\t\tset_last_err_string(\"Fail create Map\");
\t\t\treturn -1;
\t\t}

\t\tfprintf(stderr, \"[libuuu] mapfile: MapViewOfFile sz=%zu\\n\", sz); fflush(stderr);
\t\tm_pDatabuffer = (uint8_t *)MapViewOfFile(m_file_map, FILE_MAP_READ, 0, 0, sz);
\t\tfprintf(stderr, \"[libuuu] mapfile: MapViewOfFile done ptr=%p err=%lu\\n\", (void*)m_pDatabuffer, GetLastError()); fflush(stderr);
\t\tm_DataSize = sz;
\t\tm_MemSize = sz;
\t\tm_allocate_way = ALLOCATION_WAYS::MMAP;
"
  src "${src}")
file(WRITE "${buffersrc}" "${src}")
