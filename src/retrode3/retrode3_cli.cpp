// Discovery-only common Retrode3 CLI (C++11).
// Available modules are listed without executing their code. Only explicitly
// enabled, trusted modules are loaded for descriptor/ABI verification.
// This is NOT yet the common cartridge operation runner.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/ndjson_protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
struct Core {
    std::string id;
    std::string name;
    std::string version;
    std::string slot;
    std::string status;
    bool enabled;
    bool usable;
    bool can_read_rom;
};

bool is_slug(std::string const &s)
{
    if (s.empty() || s.size() > 48) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return false;
    }
    return true;
}

bool ends_with(std::string const &s, std::string const &suffix)
{
    return s.size() > suffix.size() &&
        s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool valid_text(std::string const &s, std::size_t max)
{
    if (s.empty() || s.size() > max) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 32 || c == 127) return false;
    }
    return true;
}

void note(std::string const &name, std::string const &reason)
{
    std::fprintf(stderr, "Retrode3 core %s: %s\n", name.c_str(), reason.c_str());
}

#ifndef RETRODE3_TESTING
bool trusted(struct stat const &st)
{
    return st.st_uid == 0 && (st.st_mode & 022) == 0;
}
#endif

bool directory_exists(std::string const &path)
{
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) {
        if (errno == ENOENT) return false;
        throw std::runtime_error("cannot inspect core directory");
    }
    if (!S_ISDIR(st.st_mode))
        throw std::runtime_error("core directory must be a real directory");
#ifndef RETRODE3_TESTING
    if (!trusted(st))
        throw std::runtime_error("core directory must be root-owned and write-protected");
#endif
    return true;
}

bool valid_installed_file(std::string const &path)
{
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;
#ifndef RETRODE3_TESTING
    if (!trusted(st)) return false;
#endif
    return true;
}

// Manifest is package-installed, root-owned static metadata. It allows
// disabled cores to be shown without dlopen (which can run constructors).
// Format: exactly four key=value lines: id, name, version, slot.
Core load_manifest(std::string const &path, std::string const &expected_id)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) throw std::runtime_error("cannot read core manifest");
    std::map<std::string, std::string> fields;
    std::string line;
    std::size_t total = 0;
    while (std::getline(input, line)) {
        total += line.size() + 1;
        if (total > 1024) throw std::runtime_error("core manifest too large");
        std::size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0)
            throw std::runtime_error("invalid core manifest entry");
        std::string key = line.substr(0, equals);
        if (key != "id" && key != "name" && key != "version" && key != "slot")
            throw std::runtime_error("unknown core manifest key");
        if (!fields.insert(std::make_pair(key, line.substr(equals + 1))).second)
            throw std::runtime_error("duplicate core manifest key");
    }
    if (input.bad()) throw std::runtime_error("core manifest read failed");
    Core c = {fields["id"], fields["name"], fields["version"],
              fields["slot"], "disabled", false, false, false};
    if (fields.size() != 4 || c.id != expected_id || !is_slug(c.id) ||
        !is_slug(c.slot) || !valid_text(c.name, 128) ||
        !valid_text(c.version, 64))
        throw std::runtime_error("invalid core manifest metadata");
    return c;
}

// The only permitted enablement symlink is the exact, absolute target
// available_dir/<id>.so. Never dlopen the symlink path itself.
bool enabled_link(std::string const &path, std::string const &target,
                  bool &present)
{
    present = false;
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) {
        if (errno == ENOENT) return false;
        throw std::runtime_error("cannot inspect core enablement");
    }
    present = true;
    if (!S_ISLNK(st.st_mode)) return false;
#ifndef RETRODE3_TESTING
    if (st.st_uid != 0) return false;
#endif
    char buffer[4096];
    ssize_t n = ::readlink(path.c_str(), buffer, sizeof(buffer));
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buffer)) return false;
    return std::string(buffer, static_cast<std::size_t>(n)) == target;
}

bool valid_operations(const retrode3_core_operations_v1 *api)
{
    return api &&
           api->abi_version == RETRODE3_CORE_OPERATION_ABI_V1 &&
           api->struct_size >= sizeof(retrode3_core_operations_v1) &&
           api->probe && api->release;
}

// -1 = invalid, 0 = valid metadata only, 1 = runnable operation ABI.
int compatible(std::string const &file, Core const &core,
               bool &can_read_rom)
{
    void *handle = ::dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) { note(core.id, "module load failed"); return -1; }
    int status = -1;
    try {
        void *symbol = ::dlsym(handle, "retrode3_core_describe_v1");
        if (symbol) {
            retrode3_core_describe_v1_fn describe =
                reinterpret_cast<retrode3_core_describe_v1_fn>(symbol);
            const retrode3_core_descriptor_v1 *d = describe();
            bool match = d && d->abi_version == RETRODE3_CORE_DISCOVERY_ABI_V1 &&
                d->struct_size >= sizeof(retrode3_core_descriptor_v1) &&
                d->id && d->name && d->version && d->slot &&
                core.id == d->id && core.name == d->name &&
                core.version == d->version && core.slot == d->slot;
            if (match) {
                status = 0;
                void *op_sym = ::dlsym(handle, "retrode3_core_get_operations_v1");
                if (op_sym) {
                    retrode3_core_operations_v1_fn getter =
                        reinterpret_cast<retrode3_core_operations_v1_fn>(op_sym);
                    const retrode3_core_operations_v1 *api = getter();
                    if (valid_operations(api)) {
                        can_read_rom = api->read_rom != NULL;
                        status = 1;
                    } else status = -1;
                }
            }
        }
    } catch (...) {
        ::dlclose(handle);
        throw;
    }
    ::dlclose(handle);
    if (status < 0) note(core.id, "incompatible discovery/operation ABI or metadata");
    return status;
}

std::vector<Core> discover(std::string const &available,
                           std::string const &enabled)
{
    if (!directory_exists(available)) return std::vector<Core>();
    bool enabled_exists = directory_exists(enabled);
    DIR *dir = ::opendir(available.c_str());
    if (!dir) throw std::runtime_error("cannot open available core directory");
    std::vector<Core> cores;
    try {
        struct dirent *entry;
        while ((entry = ::readdir(dir)) != NULL) {
            std::string filename(entry->d_name);
            if (!ends_with(filename, ".core")) continue;
            std::string id = filename.substr(0, filename.size() - 5);
            if (!is_slug(id)) { note(filename, "invalid core filename"); continue; }
            std::string base = available + "/" + id;
            if (!valid_installed_file(base + ".core") ||
                !valid_installed_file(base + ".so")) {
                note(id, "missing or untrusted packaged module/manifest");
                continue;
            }
            Core core;
            try { core = load_manifest(base + ".core", id); }
            catch (std::exception const &e) { note(id, e.what()); continue; }
            bool present = false;
            bool valid = enabled_exists &&
                enabled_link(enabled + "/" + id + ".so", base + ".so", present);
            core.enabled = present;
            if (present && !valid) {
                core.status = "invalid_link";
                note(id, "enablement must be a trusted link to the packaged module");
            } else if (valid) {
                int result = compatible(base + ".so", core, core.can_read_rom);
                core.usable = (result == 1);
                core.status = result == 1 ? "ready" :
                    (result == 0 ? "discovered" : "incompatible");
            }
            cores.push_back(core);
        }
        ::closedir(dir);
    } catch (...) {
        ::closedir(dir);
        throw;
    }
    std::sort(cores.begin(), cores.end(),
              [](Core const &a, Core const &b) { return a.id < b.id; });
    return cores;
}

std::string to_json(std::vector<Core> const &cores,
                    std::string const &request_id = "cli")
{
    std::string json = "{\"protocol\":\"retrode3-ndjson\",\"version\":1,"
        "\"event\":\"cores\",\"request_id\":" +
        retrode3::NdjsonProtocol::quote(request_id) + ","
        "\"discovery_abi\":1,\"cores\":[";
    for (std::size_t i = 0; i < cores.size(); ++i) {
        Core const &c = cores[i];
        if (i) json += ",";
        json += "{\"id\":" + retrode3::NdjsonProtocol::quote(c.id)
              + ",\"name\":" + retrode3::NdjsonProtocol::quote(c.name)
              + ",\"version\":" + retrode3::NdjsonProtocol::quote(c.version)
              + ",\"slot\":" + retrode3::NdjsonProtocol::quote(c.slot)
              + ",\"installed\":true,\"enabled\":"
              + (c.enabled ? "true" : "false")
              + ",\"status\":" + retrode3::NdjsonProtocol::quote(c.status)
              + ",\"usable\":" + (c.usable ? "true" : "false")
              + ",\"actions\":" + (c.usable
                    ? (c.can_read_rom ? "[\"probe\",\"read_rom\"]" : "[\"probe\"]")
                    : "[]") + "}";
    }
    return json + "]}";
}

std::string prefix(std::string const &event, std::string const &request_id)
{
    return "{\"protocol\":\"retrode3-ndjson\",\"version\":1,\"event\":"
         + retrode3::NdjsonProtocol::quote(event) + ","
         + retrode3::NdjsonProtocol::id(request_id);
}

void emit_error(retrode3::NdjsonProtocol &stream, std::string const &id,
                std::string const &code, std::string const &why)
{
    stream.emit(prefix("error", id)
       + ",\"code\":" + retrode3::NdjsonProtocol::quote(code)
       + ",\"message\":" + retrode3::NdjsonProtocol::quote(why) + "}");
}

Core const &find_usable(std::vector<Core> const &cores, std::string const &id)
{
    for (std::size_t i = 0; i < cores.size(); ++i) {
        if (cores[i].id != id) continue;
        if (!cores[i].enabled)
            throw std::runtime_error("core is disabled");
        if (!cores[i].usable)
            throw std::runtime_error("core has no compatible operation ABI");
        return cores[i];
    }
    throw std::runtime_error("core is not installed");
}

struct OpenCore {
    void *handle;
    const retrode3_core_operations_v1 *api;
    explicit OpenCore(std::string const &module) : handle(NULL), api(NULL) {
        handle = ::dlopen(module.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) throw std::runtime_error("could not load enabled core");
        try {
            void *symbol = ::dlsym(handle, "retrode3_core_get_operations_v1");
            if (!symbol) throw std::runtime_error("core operation ABI missing");
            retrode3_core_operations_v1_fn getter =
                reinterpret_cast<retrode3_core_operations_v1_fn>(symbol);
            api = getter();
            if (!valid_operations(api))
                throw std::runtime_error("core operation ABI invalid");
        } catch (...) {
            ::dlclose(handle);
            handle = NULL;
            throw;
        }
    }
    ~OpenCore() {
        if (api && api->release) api->release();
        if (handle) ::dlclose(handle);
    }
private:
    OpenCore(OpenCore const &);
    OpenCore &operator=(OpenCore const &);
};

std::string safe_cstr(char const *value, std::size_t max)
{
    void const *end = std::memchr(value, '\0', max);
    if (!end) throw std::runtime_error("unterminated core response");
    return std::string(value, static_cast<const char *>(end));
}

retrode3_probe_v1 probe(OpenCore &core)
{
    retrode3_probe_v1 result = {};
    result.struct_size = sizeof(result);
    char error[256] = {};
    if (core.api->probe(&result, error, sizeof(error)) != 0)
        throw std::runtime_error(error[0] ? safe_cstr(error, sizeof(error))
                                          : "core probe failed");
    if (result.struct_size != sizeof(result))
        throw std::runtime_error("invalid probe response ABI");
    std::string title = safe_cstr(result.title, sizeof(result.title));
    std::string mapping = safe_cstr(result.mapping, sizeof(result.mapping));
    safe_cstr(result.checksum, sizeof(result.checksum));
    if (title.empty() || mapping.empty() || result.rom_size_bytes < 512 ||
        result.rom_size_bytes > 128ull * 1024 * 1024)
        throw std::runtime_error("invalid cartridge metadata");
    return result;
}

void emit_probe(retrode3::NdjsonProtocol &stream, std::string const &id,
                std::string const &system, retrode3_probe_v1 const &probe_info)
{
    stream.emit(prefix("probe_result", id)
        + ",\"system\":" + retrode3::NdjsonProtocol::quote(system)
        + ",\"mapping\":" + retrode3::NdjsonProtocol::quote(probe_info.mapping)
        + ",\"size_bytes\":" + std::to_string(probe_info.rom_size_bytes)
        + ",\"title\":" + retrode3::NdjsonProtocol::quote(probe_info.title)
        + ",\"checksum\":" + retrode3::NdjsonProtocol::quote(probe_info.checksum)
        + ",\"candidates\":[],\"candidate_lookup\":\"not_integrated\"}");
}

void emit_capabilities(retrode3::NdjsonProtocol &stream, std::string const &id,
                       Core const &core)
{
    stream.emit(prefix("capabilities", id)
        + ",\"system\":" + retrode3::NdjsonProtocol::quote(core.id)
        + ",\"core\":" + retrode3::NdjsonProtocol::quote(core.id)
        + ",\"slot\":" + retrode3::NdjsonProtocol::quote(core.slot)
        + ",\"enabled\":true,\"usable\":true,\"commands\":"
        + (core.can_read_rom ? "[\"probe\",\"read_rom\",\"cancel\",\"get_capabilities\"]"
                             : "[\"probe\",\"get_capabilities\"]")
        + ",\"write_operations\":false,\"database_lookup\":false}");
}

struct OutputSink {
    std::FILE *file;
    retrode3::NdjsonProtocol *stream;
    std::string request_id;
    std::size_t total;
    std::size_t done;
    std::size_t next_event;
    std::size_t increment;
    std::uint32_t crc;
    std::string failure;
};

void emit_progress(OutputSink &ctx, unsigned percent)
{
    ctx.stream->emit(prefix("progress", ctx.request_id)
        + ",\"phase\":\"read_rom\",\"bytes_done\":"
        + std::to_string(ctx.done)
        + ",\"bytes_total\":" + std::to_string(ctx.total)
        + ",\"percent\":" + std::to_string(percent) + "}");
}

int is_cancelled(void *opaque)
{
    OutputSink &ctx = *static_cast<OutputSink *>(opaque);
    try {
        ctx.stream->poll_cancel(ctx.request_id);
        return ctx.stream->cancelled() ? 1 : 0;
    } catch (std::exception const &e) {
        ctx.failure = e.what();
        return 1;
    }
}

int output_bytes(void *opaque, const void *bytes, size_t count)
{
    OutputSink &ctx = *static_cast<OutputSink *>(opaque);
    try {
        if (is_cancelled(opaque)) {
            if (ctx.failure.empty()) ctx.failure = "cancelled";
            return -1;
        }
        if ((!bytes && count) || count > ctx.total - ctx.done) {
            ctx.failure = "core emitted more bytes than detected ROM size";
            return -1;
        }
        if (count && std::fwrite(bytes, 1, count, ctx.file) != count) {
            ctx.failure = "ROM output write failed";
            return -1;
        }
        unsigned char const *p = static_cast<unsigned char const *>(bytes);
        for (std::size_t i = 0; i < count; ++i) {
            ctx.crc ^= p[i];
            for (unsigned j = 0; j < 8; ++j)
                ctx.crc = (ctx.crc >> 1) ^
                    ((ctx.crc & 1u) ? 0xedb88320u : 0u);
        }
        ctx.done += count;
        if (ctx.done >= ctx.next_event && ctx.done < ctx.total) {
            unsigned percent = static_cast<unsigned>((ctx.done * 100u) / ctx.total);
            if (percent > 99) percent = 99;
            emit_progress(ctx, percent);
            ctx.next_event = ctx.done + ctx.increment;
        }
        return 0;
    } catch (std::exception const &e) {
        ctx.failure = e.what();
        return -1;
    }
}

void read_rom(retrode3::NdjsonProtocol &stream,
              retrode3::WebCommand const &command,
              OpenCore &core, retrode3_probe_v1 const &info)
{
    if (!core.api->read_rom)
        throw std::runtime_error("read_rom unavailable for this core");
    if (command.has_size && command.size_bytes != info.rom_size_bytes)
        throw std::runtime_error("size override unsupported by this core stage");
    if (!command.mapping.empty() && command.mapping != info.mapping)
        throw std::runtime_error("mapping override unsupported by this core stage");
    std::string const &output = command.output;
    // The separate web backend MUST additionally constrain output roots.
    // Do not accept relative paths, traversal or control characters.
    if (output.empty() || output[0] != '/' || output.size() > 1024 ||
        output.find("/../") != std::string::npos ||
        output.find("/./") != std::string::npos ||
        (output.size() >= 3 && output.compare(output.size()-3, 3, "/..") == 0))
        throw std::runtime_error("invalid absolute output path");
    if (::access(output.c_str(), F_OK) == 0)
        throw std::runtime_error("output already exists");
    std::string pattern = output + ".partial-XXXXXX";
    std::vector<char> tmp(pattern.begin(), pattern.end());
    tmp.push_back('\0');
    int fd = ::mkstemp(tmp.data());
    if (fd < 0) throw std::runtime_error("cannot create output file");
    std::FILE *file = ::fdopen(fd, "wb");
    if (!file) {
        ::close(fd);
        ::unlink(tmp.data());
        throw std::runtime_error("cannot open output stream");
    }
    try {
        std::size_t bytes = static_cast<std::size_t>(info.rom_size_bytes);
        std::size_t step = bytes / 100;
        if (step < 4096) step = 4096;
        OutputSink ctx = {file, &stream, command.request_id,
                          bytes, 0, step, step, 0xffffffffu, ""};
        stream.reset_cancel();
        emit_progress(ctx, 0);
        retrode3_host_io_v1 io = {RETRODE3_CORE_OPERATION_ABI_V1, sizeof(io),
                                   &ctx, output_bytes, is_cancelled};
        char error[256] = {};
        int rc = core.api->read_rom(&info, &io, error, sizeof(error));
        if (rc != 0) {
            if (ctx.failure.empty()) {
                if (stream.cancelled()) ctx.failure = "cancelled";
                else ctx.failure = error[0] ? safe_cstr(error, sizeof(error))
                                            : "core read failed";
            }
            throw std::runtime_error(ctx.failure);
        }
        if (ctx.done != bytes)
            throw std::runtime_error("short core ROM output");
        if (std::fflush(file) || ::fsync(::fileno(file)))
            throw std::runtime_error("failed to sync ROM output");
        if (std::fclose(file)) {
            file = NULL;
            throw std::runtime_error("cannot close ROM output");
        }
        file = NULL;
        if (::link(tmp.data(), output.c_str()) != 0)
            throw std::runtime_error("cannot publish ROM without overwrite");
        ::unlink(tmp.data());
        emit_progress(ctx, 100);
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08X", ~ctx.crc);
        stream.emit(prefix("result", command.request_id)
            + ",\"ok\":true,\"system\":"
            + retrode3::NdjsonProtocol::quote(command.core)
            + ",\"bytes_written\":" + std::to_string(ctx.done)
            + ",\"crc32\":" + retrode3::NdjsonProtocol::quote(hex)
            + ",\"output\":" + retrode3::NdjsonProtocol::quote(output)
            + ",\"verified\":null}");
    } catch (...) {
        if (file) std::fclose(file);
        ::unlink(tmp.data());
        throw;
    }
}

void interactive(std::string const &available, std::string const &enabled)
{
    retrode3::NdjsonProtocol stream(true);
    stream.emit("{\"protocol\":\"retrode3-ndjson\",\"version\":1,"
                "\"event\":\"ready\",\"system\":\"multi\"}");
    retrode3::WebCommand command;
    while (stream.next(command)) {
        try {
            if (command.action == "list_cores") {
                stream.emit(to_json(discover(available, enabled), command.request_id));
                continue;
            }
            if (command.action == "cancel") {
                emit_error(stream, command.request_id, "no_active_job",
                           "nothing to cancel");
                continue;
            }
            if (command.core.empty())
                throw std::runtime_error("missing core identifier");
            std::vector<Core> cores = discover(available, enabled);
            Core core_info = find_usable(cores, command.core);
            if (command.action == "get_capabilities") {
                emit_capabilities(stream, command.request_id, core_info);
                continue;
            }
            OpenCore module(available + "/" + core_info.id + ".so");
            retrode3_probe_v1 info = probe(module);
            if (command.action == "probe") emit_probe(stream, command.request_id,
                                                       core_info.id, info);
            else if (command.action == "read_rom")
                read_rom(stream, command, module, info);
        } catch (std::exception const &e) {
            std::string why(e.what());
            emit_error(stream, command.request_id,
                       why == "cancelled" ? "cancelled" : "operation_failed", why);
        }
    }
}

}

int main(int argc, char **argv)
{
    std::string available = "/usr/lib/retrode3/cores-available";
    std::string enabled = "/etc/retrode3/cores-enabled";
    bool list = false, streamed = false, run_interactive = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list-cores" && !list) list = true;
        else if (arg == "--interactive" && !run_interactive) run_interactive = true;
        else if (arg == "--protocol" && i + 1 < argc &&
                 std::string(argv[i + 1]) == "ndjson-v1") {
            ++i;
            streamed = true;
        }
#ifdef RETRODE3_TESTING
        else if (arg == "--available-dir" && i + 1 < argc)
            available = argv[++i];
        else if (arg == "--enabled-dir" && i + 1 < argc)
            enabled = argv[++i];
#endif
        else {
            std::fprintf(stderr,
                "usage: retrode3-cli --list-cores [--protocol ndjson-v1] "
                "| --protocol ndjson-v1 --interactive\n");
            return 2;
        }
    }
    if (list == run_interactive || (run_interactive && !streamed)) {
        std::fprintf(stderr, "choose --list-cores or --interactive with NDJSON\n");
        return 2;
    }
    try {
        if (list)
            retrode3::NdjsonProtocol(false).emit(to_json(discover(available, enabled)));
        else interactive(available, enabled);
        return 0;
    } catch (std::exception const &e) {
        std::fprintf(stderr, "Retrode3 host failed: %s\n", e.what());
        return 2;
    }
}
