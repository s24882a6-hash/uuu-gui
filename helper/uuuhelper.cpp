// uuu-helper — a thin libuuu-backed worker spawned by the GUI, one process per
// device per flash phase. Each invocation drives a single libuuu run and reports
// structured progress as JSON lines on stdout, e.g.:
//
//   {"event":"phase_start"}
//   {"event":"log","msg":"..."}
//   {"event":"progress","pct":42}
//   {"event":"done","success":true}
//   {"event":"done","success":false,"error":"...","permission":true}
//
// Because libuuu keeps global process state (single notify callback, global USB
// filters), the GUI runs each flash as a separate process — exactly like it used
// to spawn the external `uuu` binary — so concurrent flashes stay isolated.
//
// Modes:
//   uuu-helper list
//   uuu-helper phase --serial S | --path P  <action>
//     actions:  --boot <file>
//               --emmcall <bootloader> <wic>
//               --cmd "<single uuu command>" [--besteffort]

#include "libuuu.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// ──────────────────────────────────────────────────────────────────────────────
// JSON output
// ──────────────────────────────────────────────────────────────────────────────

static std::mutex g_out_mtx;

static std::string json_escape(const char* s)
{
    std::string out;
    if (!s) return out;
    for (const char* p = s; *p; ++p) {
        switch (*p) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(*p) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", *p);
                out += buf;
            } else {
                out += *p;
            }
        }
    }
    return out;
}

static void emit(const std::string& line)
{
    std::lock_guard<std::mutex> lk(g_out_mtx);
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

static void emit_log(const std::string& msg)
{
    // Trim trailing whitespace/newlines (device responses often carry them) and
    // skip messages that are empty once trimmed, so the log has no blank lines.
    size_t end = msg.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return;
    emit("{\"event\":\"log\",\"msg\":\"" + json_escape(msg.substr(0, end + 1).c_str()) + "\"}");
}

static void emit_progress(int pct)
{
    emit("{\"event\":\"progress\",\"pct\":" + std::to_string(pct) + "}");
}

// ──────────────────────────────────────────────────────────────────────────────
// Notify callback → progress / log
// ──────────────────────────────────────────────────────────────────────────────

static std::mutex  g_state_mtx;            // libuuu may invoke the callback from several threads
static uint64_t    g_trans_size     = 0;
static int         g_last_pct       = -1;  // last % sent to the progress bar
static int         g_last_logged_pct = 0;  // last % written to the text log
static int         g_status         = 0;   // accumulated non-zero command status
static std::string g_err;                  // first error string seen

static int notify_cb(uuu_notify nt, void*)
{
    std::lock_guard<std::mutex> lk(g_state_mtx);
    switch (nt.type) {
    case uuu_notify::NOTIFY_DEV_ATTACH:
        g_status = 0;
        emit_log(std::string("Device attached: ") + (nt.str ? nt.str : ""));
        break;

    case uuu_notify::NOTIFY_TRANS_SIZE:
    case uuu_notify::NOTIFY_DECOMPRESS_SIZE:
        g_trans_size      = nt.total;
        g_last_pct        = -1;
        g_last_logged_pct = 0;
        break;

    case uuu_notify::NOTIFY_TRANS_POS:
    case uuu_notify::NOTIFY_DECOMPRESS_POS:
        if (g_trans_size) {
            int pct = static_cast<int>(nt.index * 100 / g_trans_size);
            if (pct > g_last_pct) {
                g_last_pct = pct;
                emit_progress(pct < 99 ? pct : 99);
            }
            // Also drop a coarse milestone into the text log every 10%.
            if (pct >= g_last_logged_pct + 10 && pct < 100) {
                g_last_logged_pct = pct - (pct % 10);
                emit_log("  " + std::to_string(g_last_logged_pct) + "%");
            }
        }
        break;

    case uuu_notify::NOTIFY_CMD_START:
        if (nt.str) emit_log(std::string("$ ") + nt.str);
        break;

    case uuu_notify::NOTIFY_CMD_INFO:
        if (nt.str) emit_log(nt.str);
        break;

    case uuu_notify::NOTIFY_WAIT_FOR:
        if (nt.str) emit_log(std::string("Waiting: ") + nt.str);
        break;

    case uuu_notify::NOTIFY_CMD_END:
        if (nt.status) {
            g_status |= nt.status;
            if (g_err.empty()) {
                const char* e = uuu_get_last_err_string();
                if (e && *e) g_err = e;
            }
        }
        break;

    default:
        break;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// list mode
// ──────────────────────────────────────────────────────────────────────────────

static int list_cb(const char* path, const char* chip, const char* pro,
                   uint16_t vid, uint16_t pid, uint16_t bcd,
                   const char* serial, void*)
{
    std::string s = "{\"event\":\"device\"";
    s += ",\"path\":\""   + json_escape(path)   + "\"";
    s += ",\"chip\":\""   + json_escape(chip)   + "\"";
    s += ",\"pro\":\""    + json_escape(pro)    + "\"";
    s += ",\"vid\":"      + std::to_string(vid);
    s += ",\"pid\":"      + std::to_string(pid);
    s += ",\"bcd\":"      + std::to_string(bcd);
    s += ",\"serial\":\"" + json_escape(serial) + "\"}";
    emit(s);
    return 0;
}

static int run_list()
{
    uuu_for_each_devices(list_cb, nullptr);
    emit("{\"event\":\"list_end\"}");
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// emmc_all built-in script (from mfgtools uuu/emmc_burn_all.lst).
// Tokens `_flash.bin` and `_image` are substituted with the bootloader and wic.
// ──────────────────────────────────────────────────────────────────────────────

static const char* kEmmcAllTemplate =
    "uuu_version 1.4.149\n"
    "SDP: boot -f _flash.bin -scanlimited 0x800000\n"
    "SDPS: boot -scanterm -f _flash.bin -scanlimited 0x800000\n"
    "SDPU: delay 1000\n"
    "SDPU: write -f _flash.bin -offset 0x57c00\n"
    "SDPU: jump -scanlimited 0x800000\n"
    "SDPV: delay 1000\n"
    "SDPV: write -f _flash.bin -skipspl -scanterm -scanlimited 0x800000\n"
    "SDPV: jump -scanlimited 0x800000\n"
    "FB: ucmd setenv fastboot_dev mmc\n"
    "FB: ucmd setenv mmcdev ${emmc_dev}\n"
    "FB: ucmd mmc dev ${emmc_dev}\n"
    "FB: flash -raw2sparse all _image\n"
    "FB: flash -scanterm -scanlimited 0x800000 bootloader _flash.bin\n"
    "FB: ucmd if env exists emmc_ack; then ; else setenv emmc_ack 0; fi;\n"
    "FB: ucmd mmc partconf ${emmc_dev} ${emmc_ack} 1 0\n"
    "FB: done\n";

// Whole-token replacement (tokens are delimited by spaces / newlines), so a
// substituted path that happens to contain a token isn't re-substituted.
static bool is_compressed(const std::string& path)
{
    // libuuu decompresses .zst/.bz2/.gz on-the-fly only when the path is
    // followed by "/*" in the script — without it the raw compressed bytes
    // are written to the device.
    auto ends = [&](const char* ext) {
        return path.size() >= std::strlen(ext) &&
               path.compare(path.size() - std::strlen(ext), std::string::npos, ext) == 0;
    };
    return ends(".zst") || ends(".bz2") || ends(".gz") || ends(".xz");
}

static std::string substitute_tokens(const std::string& tmpl,
                                     const std::string& flashBin,
                                     const std::string& image)
{
    std::string out;
    std::string tok;
    // Append "/*" after a compressed image path so libuuu decompresses it.
    const std::string imageToken = is_compressed(image)
        ? "\"" + image + "/*\""
        : "\"" + image + "\"";
    auto flush = [&]() {
        // Quote substituted paths so spaces/parentheses in filenames don't get
        // split by uuu's whitespace-based script tokenizer.
        if (tok == "_flash.bin")    out += "\"" + flashBin + "\"";
        else if (tok == "_image")   out += imageToken;
        else                        out += tok;
        tok.clear();
    };
    for (char c : tmpl) {
        if (c == ' ' || c == '\n' || c == '\t') {
            flush();
            out += c;
        } else {
            tok += c;
        }
    }
    flush();
    return out;
}

// ──────────────────────────────────────────────────────────────────────────────
// phase mode
// ──────────────────────────────────────────────────────────────────────────────

static std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static int finish(int rc, bool bestEffort)
{
    bool ok = (rc == 0 && g_status == 0) || bestEffort;

    std::string err = g_err;
    if (!ok && err.empty()) {
        const char* e = uuu_get_last_err_string();
        if (e && *e) err = e;
    }

    // Recognise the various ways libusb reports "needs elevated privileges":
    //  - LIBUSB_ERROR_ACCESS (-3), printed as "(-3)" or by name
    //  - "detach kernel driver failure" / failure to claim the interface
    //    (on macOS claiming an NXP device requires root)
    bool permission = false;
    if (!ok) {
        std::string le = lower(err);
        if (le.find("access") != std::string::npos ||
            le.find("permission") != std::string::npos ||
            le.find("sudo") != std::string::npos ||
            le.find("detach kernel driver") != std::string::npos ||
            le.find("claim") != std::string::npos ||
            le.find("(-3)") != std::string::npos)
            permission = true;
    }

    if (ok) {
        emit_progress(100);
        emit("{\"event\":\"done\",\"success\":true}");
        return 0;
    }
    emit("{\"event\":\"done\",\"success\":false,\"error\":\""
         + json_escape(err.c_str()) + "\",\"permission\":"
         + (permission ? "true" : "false") + "}");
    return permission ? 2 : 1;
}

static int run_phase(const std::vector<std::string>& args)
{
    std::string serial, path, boot, bootloader, wic, cmd;
    bool haveBoot = false, haveEmmc = false, haveCmd = false, bestEffort = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= args.size()) {
                emit_log(std::string("Missing argument for ") + name);
                return {};
            }
            return args[++i];
        };
        if (a == "--serial")        serial = next("--serial");
        else if (a == "--path")     path   = next("--path");
        else if (a == "--boot")   { boot   = next("--boot"); haveBoot = true; }
        else if (a == "--emmcall"){ bootloader = next("--emmcall"); wic = next("--emmcall"); haveEmmc = true; }
        else if (a == "--cmd")    { cmd    = next("--cmd"); haveCmd = true; }
        else if (a == "--besteffort") bestEffort = true;
    }

    // Target a single device. Prefer serial — it survives USB re-enumeration
    // between SDP and fastboot, so libuuu re-finds the board on its own.
    if (!serial.empty())     uuu_add_usbserial_no_filter(serial.c_str());
    else if (!path.empty())  uuu_add_usbpath_filter(path.c_str());

    uuu_register_notify_callback(notify_cb, nullptr);
    emit("{\"event\":\"phase_start\"}");

    int rc = 0;
    if (haveBoot) {
        rc = uuu_auto_detect_file(boot.c_str());
        if (rc == 0) rc = uuu_wait_uuu_finish(0, 0);
    } else if (haveEmmc) {
        std::string script = substitute_tokens(kEmmcAllTemplate, bootloader, wic);
        rc = uuu_run_cmd_script(script.c_str(), 0);
        if (rc == 0) rc = uuu_wait_uuu_finish(0, 0);
    } else if (haveCmd) {
        // A command like "FB: reboot" disconnects the device, so
        // uuu_wait_uuu_finish would otherwise block forever waiting for it to
        // come back. Bound the waits so this (best-effort) phase returns.
        uuu_set_wait_timeout(10);       // up to 10s for the device to appear
        uuu_set_wait_next_timeout(3);   // return ~3s after it drops off the bus
        std::string script = "uuu_version 1.4.0\n" + cmd + "\n";
        rc = uuu_run_cmd_script(script.c_str(), 0);
        if (rc == 0) rc = uuu_wait_uuu_finish(0, 0);
    } else {
        emit_log("No action specified");
        rc = 1;
    }

    return finish(rc, bestEffort);
}

// ──────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: uuu-helper list | phase <opts>\n");
        return 64;
    }

    std::string mode = argv[1];
    std::vector<std::string> rest(argv + 2, argv + argc);

    if (mode == "list")  return run_list();
    if (mode == "phase") return run_phase(rest);

    std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
    return 64;
}
