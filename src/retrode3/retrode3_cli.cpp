// Discovery-only common Retrode3 CLI (C++11).
// Available modules are listed without executing their code. Only explicitly
// enabled, trusted modules are loaded for descriptor/ABI verification.
// This is NOT yet the common cartridge operation runner.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/core_operations_v1.h"
#include "retrode3/core_operations_v2.h"
#include "retrode3/core_operations_v3.h"
#include "retrode3/json_v3.h"
#include "retrode3/ndjson_protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/file.h>
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
    bool has_v2;
    bool has_v3;
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
              fields["slot"], "disabled", false, false, false, false, false};
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
               bool &can_read_rom, bool &has_v2, bool &has_v3)
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
                        void *v2sym = ::dlsym(handle, "retrode3_core_get_operations_v2");
                        if (v2sym) {
                            retrode3_core_operations_v2_fn getv2 =
                                reinterpret_cast<retrode3_core_operations_v2_fn>(v2sym);
                            const retrode3_core_operations_v2 *v2 = getv2();
                            if (!v2 || v2->abi_version != RETRODE3_CORE_OPERATION_ABI_V2 ||
                                v2->struct_size < sizeof(*v2) ||
                                !v2->fast_probe || !v2->read_rom) {
                                status = -1;
                            } else {
                                has_v2 = true;
                                can_read_rom = true;
                                status = 1;
                            }
                        } else status = 1;
                        if (status == 1) {
                            void *v3sym = ::dlsym(handle, "retrode3_core_get_operations_v3");
                            if (v3sym) {
                                retrode3_core_operations_v3_fn getv3 =
                                    reinterpret_cast<retrode3_core_operations_v3_fn>(v3sym);
                                const retrode3_core_operations_v3 *v3 = getv3();
                                if (!v3 ||
                                    v3->abi_version != RETRODE3_CORE_OPERATION_ABI_V3 ||
                                    v3->struct_size < sizeof(*v3) ||
                                    !v3->capabilities_json || !v3->fast_probe_json ||
                                    !v3->plan_read || !v3->read_rom)
                                    status = -1;
                                else { has_v3 = true; can_read_rom = true; }
                            }
                        }
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
                int result = compatible(base + ".so", core, core.can_read_rom, core.has_v2, core.has_v3);
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

// Single lock covering the shared cartridge bus for all NEW common-CLI
// operations. The shipping OS must create /run/lock/retrode3 securely.
// Existing legacy acceptance binaries do not yet participate in this lock.
struct BusLock {
    int fd;
    explicit BusLock(std::string const &enabled_dir) : fd(-1) {
#ifdef RETRODE3_TESTING
        std::string path = enabled_dir + "/.bus.lock";
#else
        (void)enabled_dir;
        std::string path = "/run/lock/retrode3/bus.lock";
#endif
        int flags = O_RDWR | O_CREAT;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        fd = ::open(path.c_str(), flags, 0660);
        if (fd < 0) throw std::runtime_error("cartridge bus lock unavailable");
        struct stat st;
        if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
            (st.st_mode & S_IWOTH)) {
            ::close(fd);
            fd = -1;
            throw std::runtime_error("unsafe cartridge bus lock file");
        }
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd);
            fd = -1;
            throw std::runtime_error("cartridge bus busy");
        }
    }
    ~BusLock() {
        if (fd >= 0) {
            ::flock(fd, LOCK_UN);
            ::close(fd);
        }
    }
private:
    BusLock(BusLock const &);
    BusLock &operator=(BusLock const &);
};

struct OpenCore {
    void *handle;
    const retrode3_core_operations_v1 *api;
    const retrode3_core_operations_v2 *api2;
    const retrode3_core_operations_v3 *api3;
    explicit OpenCore(std::string const &module) : handle(NULL), api(NULL), api2(NULL), api3(NULL) {
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
            void *v2sym = ::dlsym(handle, "retrode3_core_get_operations_v2");
            if (v2sym) {
                retrode3_core_operations_v2_fn getv2 =
                    reinterpret_cast<retrode3_core_operations_v2_fn>(v2sym);
                api2 = getv2();
                if (!api2 || api2->abi_version != RETRODE3_CORE_OPERATION_ABI_V2 ||
                    api2->struct_size < sizeof(*api2) ||
                    !api2->fast_probe || !api2->read_rom)
                    throw std::runtime_error("core v2 ABI invalid");
            }
            void *v3sym = ::dlsym(handle, "retrode3_core_get_operations_v3");
            if (v3sym) {
                retrode3_core_operations_v3_fn getv3 =
                    reinterpret_cast<retrode3_core_operations_v3_fn>(v3sym);
                api3 = getv3();
                if (!api3 || api3->abi_version != RETRODE3_CORE_OPERATION_ABI_V3 ||
                    api3->struct_size < sizeof(*api3) ||
                    !api3->capabilities_json || !api3->fast_probe_json ||
                    !api3->plan_read || !api3->read_rom)
                    throw std::runtime_error("core v3 ABI invalid");
            }
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
    // GCC 4.9 on Retrode3 warns for aggregate `= {}` under -Werror.
    // Zero the entire C ABI struct explicitly before passing it to a core.
    retrode3_probe_v1 result;
    std::memset(&result, 0, sizeof(result));
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


retrode3_probe_v2 fast_probe(OpenCore &core)
{
    if (!core.api2) throw std::runtime_error("core lacks fast-probe ABI");
    retrode3_probe_v2 info;
    std::memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    char error[256] = {};
    if (core.api2->fast_probe(&info, error, sizeof(error)) != 0)
        throw std::runtime_error(error[0] ? safe_cstr(error, sizeof(error))
                                          : "fast probe failed");
    if (info.struct_size != sizeof(info) ||
        info.candidate_count > RETRODE3_MAX_CANDIDATES_V2 ||
        info.layout_count > RETRODE3_MAX_LAYOUTS_V2 ||
        (std::strcmp(info.identification_status, "unknown") != 0 &&
         std::strcmp(info.identification_status, "unique") != 0 &&
         std::strcmp(info.identification_status, "ambiguous") != 0))
        throw std::runtime_error("invalid v2 probe response");
    if (std::strcmp(info.identification_status, "unknown") == 0 &&
        info.candidate_count != 0)
        throw std::runtime_error("inconsistent unknown NES probe");
    for (unsigned i = 0; i < info.candidate_count; ++i) {
        retrode3_candidate_v2 const &c = info.candidates[i];
        if (!valid_text(safe_cstr(c.id, sizeof(c.id)), 31) ||
            !valid_text(safe_cstr(c.title, sizeof(c.title)), 127) ||
            !valid_text(safe_cstr(c.layout_id, sizeof(c.layout_id)), 31))
            throw std::runtime_error("invalid v2 candidate strings");
    }
    for (unsigned i = 0; i < info.layout_count; ++i)
        if (!valid_text(safe_cstr(info.layouts[i].id,
                                 sizeof(info.layouts[i].id)), 31))
            throw std::runtime_error("invalid v2 layout ID");
    return info;
}

void emit_fast_probe(retrode3::NdjsonProtocol &stream,
                     std::string const &id, std::string const &system,
                     retrode3_probe_v2 const &info)
{
    const bool one = info.layout_count == 1;
    std::string title = info.candidate_count == 1
                      ? info.candidates[0].title : "NES cartridge";
    std::string json = prefix("probe_result", id)
        + ",\"system\":" + retrode3::NdjsonProtocol::quote(system)
        + ",\"identification_status\":"
        + retrode3::NdjsonProtocol::quote(info.identification_status)
        + ",\"mapping\":" + retrode3::NdjsonProtocol::quote(
            one ? (info.layouts[0].mapper == 0 ? "nrom" :
                   info.layouts[0].mapper == 1 ? "mmc1" : "unknown")
                : "unknown")
        + ",\"title\":" + retrode3::NdjsonProtocol::quote(title)
        + ",\"size_bytes\":" + std::to_string(
            one ? (16u + (info.layouts[0].prg_kib + info.layouts[0].chr_kib) * 1024u) : 0u)
        + ",\"checksum\":null,\"candidate_lookup\":\"fingerprint_only\""
        + ",\"selection_required\":" +
            (info.layout_count != 1 || !info.layouts[0].read_supported ?
             "true" : "false")
        + ",\"candidates\":[";
    for (unsigned i = 0; i < info.candidate_count; ++i) {
        if (i) json += ",";
        retrode3_candidate_v2 const &c = info.candidates[i];
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08X", c.payload_crc32);
        json += "{\"id\":" + retrode3::NdjsonProtocol::quote(c.id)
             + ",\"title\":" + retrode3::NdjsonProtocol::quote(c.title)
             + ",\"layout_id\":" + retrode3::NdjsonProtocol::quote(c.layout_id)
             + ",\"mapper\":" + std::to_string(c.mapper)
             + ",\"submapper\":" + std::to_string(c.submapper)
             + ",\"prg_kib\":" + std::to_string(c.prg_kib)
             + ",\"chr_kib\":" + std::to_string(c.chr_kib)
             + ",\"payload_crc32_expected\":" + retrode3::NdjsonProtocol::quote(hex)
             + ",\"read_supported\":" + (c.read_supported ? "true" : "false") + "}";
    }
    json += "],\"read_layouts\":[";
    for (unsigned i = 0; i < info.layout_count; ++i) {
        if (i) json += ",";
        retrode3_layout_v2 const &l = info.layouts[i];
        json += "{\"id\":" + retrode3::NdjsonProtocol::quote(l.id)
             + ",\"mapper\":" + std::to_string(l.mapper)
             + ",\"submapper\":" + std::to_string(l.submapper)
             + ",\"prg_kib\":" + std::to_string(l.prg_kib)
             + ",\"chr_kib\":" + std::to_string(l.chr_kib)
             + ",\"read_supported\":" + (l.read_supported ? "true" : "false") + "}";
    }
    stream.emit(json + "]}");
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
        + ",\"write_operations\":false,\"database_lookup\":"
        + (core.has_v2 ? "true" : "false")
        + (core.has_v2
           ? ",\"fast_probe\":true,\"manual_layout\":true,"
             "\"supported_mappers\":[{\"mapper\":0,\"submapper\":0,"
             "\"prg_kib\":[16,32],\"chr_kib\":[0,8],\"read_supported\":true},"
             "{\"mapper\":1,\"submapper\":0,"
             "\"prg_kib\":[32,64,128,256,512],"
             "\"chr_kib\":[0,8,16,32,64,128],"
             "\"read_supported\":true},"
             "{\"mapper\":1,\"submapper\":5,"
             "\"prg_kib\":[32],"
             "\"chr_kib\":[8,16,32,64,128],"
             "\"read_supported\":true},"
             "{\"mapper\":2,\"submapper\":0,\"read_supported\":false},"
             "{\"mapper\":3,\"submapper\":0,\"read_supported\":false},"
             "{\"mapper\":4,\"submapper\":0,\"read_supported\":false},"
             "{\"mapper\":7,\"submapper\":0,\"read_supported\":false}]"
           : "") + "}");
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
        + std::to_string(percent == 0 ? 0u : ctx.done)
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
            unsigned percent = static_cast<unsigned>((static_cast<std::uint64_t>(ctx.done) * 100u) / ctx.total);
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


bool nes_power_two(unsigned n) { return n != 0 && !(n & (n-1)); }
bool nes_manual_geometry(unsigned m, unsigned sub, unsigned p, unsigned c)
{
    if (m == 1)
        return (sub == 0 || (sub == 5 && p == 32)) &&
               (p == 32 || p == 64 || p == 128 ||
                p == 256 || p == 512) &&
               (c == 0 || c == 8 || c == 16 || c == 32 ||
                c == 64 || c == 128);
    if (sub != 0) return false;
    switch (m) {
    case 0: return (p == 16 || p == 32) && (c == 0 || c == 8);
    case 2: return nes_power_two(p) && p >= 64 && p <= 256 && c == 0;
    case 3: return (p == 16 || p == 32) &&
                   nes_power_two(c) && c >= 8 && c <= 32;
    case 4: return nes_power_two(p) && p >= 32 && p <= 512 &&
                   (c == 0 || (nes_power_two(c) && c >= 8 && c <= 256));
    case 7: return nes_power_two(p) && p >= 64 && p <= 256 && c == 0;
    default: return false; // all other layouts remain blocked
    }
}

/* v2 output path: reserve header, stream payload ONCE from cartridge,
 * patch the chosen header in the private file, verify file CRC locally,
 * and publish atomically. This is intentionally not an NES-specific host
 * scanner: only the NES core knows the cartridge layout. */
void read_rom_v2(retrode3::NdjsonProtocol &stream,
                 retrode3::WebCommand const &command,
                 OpenCore &core, retrode3_probe_v2 const &probe_info)
{
    if (!core.api2 || !core.api2->read_rom)
        throw std::runtime_error("core does not support read_rom v2");
    if (command.has_size || !command.mapping.empty())
        throw std::runtime_error("legacy size/mapping fields unsupported for NES v2");
    retrode3_read_request_v2 request;
    std::memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    std::snprintf(request.candidate_id, sizeof(request.candidate_id), "%s",
                  command.candidate_id.c_str());
    std::snprintf(request.layout_id, sizeof(request.layout_id), "%s",
                  command.layout_id.c_str());
    unsigned prg = 0, chr = 0, mapper = 0, submapper = 0;
    if (command.has_manual) {
        request.manual = 1;
        request.mapper = command.mapper;
        request.submapper = command.submapper;
        request.prg_kib = command.prg_kib;
        request.chr_kib = command.chr_kib;
        request.battery = static_cast<std::uint8_t>(command.battery);
        request.mirroring = static_cast<std::uint8_t>(
            command.mirroring == "vertical" ? 1u :
            command.mirroring == "four_screen" ? 2u : 0u);
        if (!nes_manual_geometry(request.mapper, request.submapper,
                                 request.prg_kib, request.chr_kib))
            throw std::runtime_error("unsupported manual NES mapper or size");
        prg = request.prg_kib;
        chr = request.chr_kib;
        mapper = request.mapper;
        submapper = request.submapper;
    } else {
        const retrode3_layout_v2 *layout = NULL;
        for (unsigned i = 0; i < probe_info.layout_count; ++i) {
            const retrode3_layout_v2 &entry = probe_info.layouts[i];
            if (!command.layout_id.empty() && command.layout_id != entry.id)
                continue;
            if (!command.candidate_id.empty()) {
                bool found = false;
                for (unsigned j = 0; j < probe_info.candidate_count; ++j)
                    if (command.candidate_id == probe_info.candidates[j].id &&
                        std::strcmp(entry.id, probe_info.candidates[j].layout_id) == 0)
                        found = true;
                if (!found) continue;
            }
            if (layout)
                throw std::runtime_error("NES layout choice required");
            layout = &entry;
        }
        if (!layout || !layout->read_supported ||
            !nes_manual_geometry(layout->mapper, layout->submapper,
                                 layout->prg_kib, layout->chr_kib))
            throw std::runtime_error("NES layout unavailable or unsupported");
        prg = layout->prg_kib;
        chr = layout->chr_kib;
        mapper = layout->mapper;
        submapper = layout->submapper;
    }
    const std::size_t total = 16u + (prg + chr) * 1024u;
    std::string const &output = command.output;
    if (output.empty() || output[0] != '/' || output.size() > 1024 ||
        output.find("/../") != std::string::npos ||
        output.find("/./") != std::string::npos ||
        (output.size() >= 3 && output.compare(output.size()-3, 3, "/..") == 0))
        throw std::runtime_error("invalid absolute output path");
    if (::access(output.c_str(), F_OK) == 0)
        throw std::runtime_error("output already exists");
    std::string pattern = output + ".partial-XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end());
    temp.push_back('\0');
    int fd = ::mkstemp(temp.data());
    if (fd < 0) throw std::runtime_error("cannot create output file");
    std::FILE *file = ::fdopen(fd, "w+b");
    if (!file) {
        ::close(fd);
        ::unlink(temp.data());
        throw std::runtime_error("cannot open temporary ROM");
    }
    try {
        unsigned char placeholder[16] = {};
        if (std::fwrite(placeholder, 1, sizeof(placeholder), file) != sizeof(placeholder))
            throw std::runtime_error("cannot reserve NES header");
        std::size_t step = total / 100;
        if (step < 4096) step = 4096;
        OutputSink ctx = {file, &stream, command.request_id,
                          total, 16u, step, step, 0xffffffffu, ""};
        stream.reset_cancel();
        emit_progress(ctx, 0);
        retrode3_host_io_v1 io = {RETRODE3_CORE_OPERATION_ABI_V1, sizeof(io),
                                   &ctx, output_bytes, is_cancelled};
        retrode3_read_result_v2 result;
        std::memset(&result, 0, sizeof(result));
        result.struct_size = sizeof(result);
        char error[256] = {};
        int rc = core.api2->read_rom(&probe_info, &request, &io, &result,
                                      error, sizeof(error));
        if (rc != 0) {
            if (ctx.failure.empty())
                ctx.failure = stream.cancelled() ? "cancelled" :
                              error[0] ? safe_cstr(error, sizeof(error))
                                       : "NES core read failed";
            throw std::runtime_error(ctx.failure);
        }
        if (result.struct_size != sizeof(result) || ctx.done != total ||
            result.payload_crc32 != ~ctx.crc ||
            std::memcmp(result.header, "NES\x1a", 4) != 0 ||
            result.header[4] != prg / 16u || result.header[5] != chr / 8u ||
            (result.header[6] >> 4 | (result.header[7] & 0xf0u)) != mapper)
            throw std::runtime_error("invalid NES v2 output/header/checksum");
        const std::string title = safe_cstr(result.title, sizeof(result.title));
        const std::string matched = safe_cstr(result.matched_candidate_id,
                                             sizeof(result.matched_candidate_id));
        const std::string layout = safe_cstr(result.layout_id, sizeof(result.layout_id));
        char expected_layout[32];
        if (mapper == 0)
            std::snprintf(expected_layout, sizeof(expected_layout),
                          "nrom-%u-%u", prg, chr);
        else if (mapper == 1 && submapper)
            std::snprintf(expected_layout, sizeof(expected_layout),
                          "m1s%u-%u-%u", submapper, prg, chr);
        else
            std::snprintf(expected_layout, sizeof(expected_layout),
                          "m%u-%u-%u", mapper, prg, chr);
        if (title.empty() || layout != expected_layout ||
            result.verified > 1 || (result.verified && matched.empty()))
            throw std::runtime_error("invalid NES result metadata");
        if (std::fflush(file) || std::fseek(file, 0, SEEK_SET) != 0 ||
            std::fwrite(result.header, 1, 16, file) != 16 ||
            std::fflush(file) || std::fseek(file, 0, SEEK_SET) != 0)
            throw std::runtime_error("cannot finalize NES header");
        std::uint32_t file_crc = 0xffffffffu;
        std::size_t seen = 0;
        unsigned char bytes[4096];
        while (true) {
            std::size_t n = std::fread(bytes, 1, sizeof(bytes), file);
            for (std::size_t i = 0; i < n; ++i) {
                file_crc ^= bytes[i];
                for (unsigned b = 0; b < 8; ++b)
                    file_crc = (file_crc >> 1) ^
                        ((file_crc & 1u) ? 0xedb88320u : 0u);
            }
            seen += n;
            if (n != sizeof(bytes)) {
                if (std::ferror(file))
                    throw std::runtime_error("cannot verify final NES file");
                break;
            }
        }
        if (seen != total || ::fsync(::fileno(file)) != 0)
            throw std::runtime_error("incomplete NES output file");
        if (std::fclose(file)) {
            file = NULL;
            throw std::runtime_error("cannot close NES output");
        }
        file = NULL;
        if (::link(temp.data(), output.c_str()) != 0)
            throw std::runtime_error("cannot publish ROM without overwrite");
        ::unlink(temp.data());
        emit_progress(ctx, 100);
        char crc_hex[9], payload_hex[9];
        std::snprintf(crc_hex, sizeof(crc_hex), "%08X", ~file_crc);
        std::snprintf(payload_hex, sizeof(payload_hex), "%08X", result.payload_crc32);
        stream.emit(prefix("result", command.request_id)
            + ",\"ok\":true,\"system\":" + retrode3::NdjsonProtocol::quote(command.core)
            + ",\"bytes_written\":" + std::to_string(total)
            + ",\"crc32\":" + retrode3::NdjsonProtocol::quote(crc_hex)
            + ",\"payload_crc32\":" + retrode3::NdjsonProtocol::quote(payload_hex)
            + ",\"output\":" + retrode3::NdjsonProtocol::quote(output)
            + ",\"title\":" + retrode3::NdjsonProtocol::quote(title)
            + ",\"layout_id\":" + retrode3::NdjsonProtocol::quote(layout)
            + ",\"matched_candidate_id\":" + retrode3::NdjsonProtocol::quote(matched)
            + ",\"verified\":" + (result.verified ? "true" : "false") + "}");
    } catch (...) {
        if (file) std::fclose(file);
        ::unlink(temp.data());
        throw;
    }
}


/* Generic v3 has no mapper, ROM geometry, voltage or console-specific data
 * in the host. Live adapter callbacks are the final safety authority. */
retrode3::JsonV3 core_document_v3(retrode3_core_json_v3_fn callback)
{
    char payload[16384] = {};
    char error[256] = {};
    if (callback(payload, sizeof(payload), error, sizeof(error)) != 0)
        throw std::runtime_error(error[0] ? safe_cstr(error, sizeof(error))
                                          : "core JSON callback failed");
    std::string json = safe_cstr(payload, sizeof(payload));
    retrode3::JsonV3 doc = retrode3::parse_json_v3(json);
    if (doc.kind != retrode3::JsonV3::Object)
        throw std::runtime_error("core JSON document must be an object");
    return doc;
}

retrode3::JsonV3 capability_schema_v3(OpenCore &module)
{
    retrode3::JsonV3 schema = core_document_v3(module.api3->capabilities_json);
    if (schema.at("schema_version").number() != 1 ||
        schema.at("schema_id").text().empty() ||
        schema.at("schema_id").text().size() > 63 ||
        schema.at("fields").kind != retrode3::JsonV3::Array ||
        schema.at("selection_modes").kind != retrode3::JsonV3::Array)
        throw std::runtime_error("invalid core selection schema");
    return schema;
}

void emit_capabilities_v3(retrode3::NdjsonProtocol &stream,
                          std::string const &id, Core const &core,
                          OpenCore &module)
{
    retrode3::JsonV3 schema = capability_schema_v3(module);
    stream.emit(prefix("capabilities", id)
        + ",\"system\":" + retrode3::NdjsonProtocol::quote(core.id)
        + ",\"core\":" + retrode3::NdjsonProtocol::quote(core.id)
        + ",\"slot\":" + retrode3::NdjsonProtocol::quote(core.slot)
        + ",\"enabled\":true,\"usable\":true,\"commands\":[\"probe\",\"read_rom\",\"cancel\",\"get_capabilities\"]"
        + ",\"write_operations\":false,\"selection_schema\":"
        + retrode3::stringify_json_v3(schema) + "}");
}

std::string probe_event_v3(std::string const &id, Core const &core,
                           OpenCore &module)
{
    retrode3::JsonV3 schema = capability_schema_v3(module);
    retrode3::JsonV3 info = core_document_v3(module.api3->fast_probe_json);
    std::string status = info.at("identification_status").text();
    std::string ambiguity = info.at("ambiguity").text();
    bool required = info.at("selection_required").kind == retrode3::JsonV3::Boolean &&
                    info.at("selection_required").boolean;
    if ((status != "unique" && status != "ambiguous" && status != "unknown") ||
        (ambiguity != "none" && ambiguity != "titles_only" &&
         ambiguity != "layouts") ||
        info.at("candidates").kind != retrode3::JsonV3::Array ||
        info.at("read_layouts").kind != retrode3::JsonV3::Array ||
        info.at("selection_required").kind != retrode3::JsonV3::Boolean ||
        (ambiguity == "layouts" && !required))
        throw std::runtime_error("invalid generic probe response");
    for (std::size_t i = 0; i < info.at("candidates").array.size(); ++i) {
        const retrode3::JsonV3 &candidate = info.at("candidates").array[i];
        if (candidate.at("id").text().empty() ||
            candidate.at("title").text().empty() ||
            candidate.at("layout_id").text().empty() ||
            candidate.at("read_supported").kind != retrode3::JsonV3::Boolean)
            throw std::runtime_error("invalid generic probe candidate");
    }
    if (status == "unknown" && !info.at("candidates").array.empty())
        throw std::runtime_error("unknown probe cannot list candidates");
    std::string out = prefix("probe_result", id)
        + ",\"system\":" + retrode3::NdjsonProtocol::quote(core.id)
        + ",\"core\":" + retrode3::NdjsonProtocol::quote(core.id)
        + ",\"slot\":" + retrode3::NdjsonProtocol::quote(core.slot)
        + ",\"selection_schema_id\":" +
            retrode3::NdjsonProtocol::quote(schema.at("schema_id").text());
    for (std::map<std::string, retrode3::JsonV3>::const_iterator it = info.object.begin();
         it != info.object.end(); ++it) {
        if (it->first == "system" || it->first == "core" ||
            it->first == "slot" || it->first == "selection_schema_id" ||
            it->first == "event" || it->first == "protocol" ||
            it->first == "version" || it->first == "request_id" ||
            it->first == "read_voltage_mv")
            throw std::runtime_error("core probe attempted envelope override");
        out += "," + retrode3::NdjsonProtocol::quote(it->first) + ":" +
            retrode3::stringify_json_v3(it->second);
    }
    // The core capability schema is authoritative for the nominal read
    // voltage. The probe body must not spoof this metadata.
    if (schema.has("read_voltage_mv")) {
        const retrode3::JsonV3 &voltage = schema.at("read_voltage_mv");
        if (voltage.kind == retrode3::JsonV3::Integer &&
            (voltage.integer == 3300 || voltage.integer == 5000))
            out += ",\"read_voltage_mv\":" + std::to_string(voltage.integer);
    }
    return out + "}";
}


void emit_probe_v3(retrode3::NdjsonProtocol &stream,
                   std::string const &id, Core const &core,
                   OpenCore &module)
{
    stream.emit(probe_event_v3(id, core, module));
}

/* Slot-oriented detection remains within ONE bus lock. Core-provided
 * identification_status describes game identity, not console identity.
 * Only the explicit system_detected boolean counts for adapter selection.
 * A failed or unsupported probe never constitutes a positive match. */
std::string read_slot_attribute(const std::string &slot, const char *attribute)
{
#ifdef RETRODE3_TESTING
    const char *root = std::getenv("RETRODE3_TEST_SLOT_ROOT");
    const std::string base = (root && *root) ? root : "/sys/class/retrode3";
#else
    const std::string base = "/sys/class/retrode3";
#endif
    std::ifstream in((base + "/slot-" + slot + "/" + attribute).c_str());
    std::string value;
    if (!in || !std::getline(in, value))
        throw std::runtime_error("slot hardware status unavailable");
    return value;
}
std::string slot_attribute_path(const std::string &slot, const char *attribute)
{
#ifdef RETRODE3_TESTING
    const char *root = std::getenv("RETRODE3_TEST_SLOT_ROOT");
    const std::string base = (root && *root) ? root : "/sys/class/retrode3";
#else
    const std::string base = "/sys/class/retrode3";
#endif
    return base + "/slot-" + slot + "/" + attribute;
}
std::string apply_voltage(const std::string &slot, unsigned requested)
{
    if (requested != 3300 && requested != 5000)
        return "unsupported";
    const std::string current = read_slot_attribute(slot, "vcc");
    const std::string target = std::to_string(requested) + "mV";
    if (current == target) return "ready";
    if (read_slot_attribute(slot, "sense") != "active")
        return "cartridge_removed";
    const std::string path = slot_attribute_path(slot, "vcc");
    // No shell, no raw GPIO. Sysfs permissions remain the privilege boundary.
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return "manual_required";
    const std::string data = std::to_string(requested) + "\n";
    ssize_t n = ::write(fd, data.data(), data.size());
    const int close_rc = ::close(fd);
    if (n != static_cast<ssize_t>(data.size()) || close_rc != 0)
        return "manual_required";
    if (read_slot_attribute(slot, "sense") != "active")
        return "cartridge_removed";
    return read_slot_attribute(slot, "vcc") == target ? "ready" : "manual_required";
}
void probe_slot(retrode3::NdjsonProtocol &stream, const std::string &request_id,
                const std::string &slot, const std::vector<Core> &cores,
                const std::string &available, const std::string &enabled)
{
    std::vector<Core> matches;
    for (std::size_t i = 0; i < cores.size(); ++i)
        if (cores[i].slot == slot && cores[i].enabled && cores[i].usable &&
            cores[i].has_v3)
            matches.push_back(cores[i]);
    std::string out = prefix("slot_probe_result", request_id)
        + ",\"slot\":" + retrode3::NdjsonProtocol::quote(slot);
    if (matches.empty()) {
        stream.emit(out + ",\"detection\":\"unavailable\",\"core\":null,"
                    "\"candidates\":[],\"attempts\":[]}");
        return;
    }
    BusLock lock(enabled);
    if (read_slot_attribute(slot, "sense") != "active") {
        stream.emit(out + ",\"detection\":\"empty\",\"core\":null,"
                    "\"candidates\":[],\"attempts\":[]}");
        return;
    }
    const std::string vcc = read_slot_attribute(slot, "vcc");
    if (vcc != "3300mV" && vcc != "5000mV") {
        stream.emit(out + ",\"detection\":\"manual_required\",\"core\":null,"
                    "\"reason\":\"unknown_probe_voltage\","
                    "\"candidates\":[],\"attempts\":[]}");
        return;
    }
    // Deterministic lexicographic ID order, with no speculative writes.
    std::string attempts = "[";
    std::string candidates = "[";
    std::string selected;
    std::string selected_event;
    std::string selected_voltage = "not_applicable";
    unsigned candidate_count = 0;
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const Core &c = matches[i];
        if (i) attempts += ",";
        attempts += "{\"core\":" + retrode3::NdjsonProtocol::quote(c.id);
        try {
            if (read_slot_attribute(slot, "sense") != "active")
                throw std::runtime_error("cartridge_removed");
            OpenCore module(available + "/" + c.id + ".so");
            retrode3::JsonV3 schema = capability_schema_v3(module);
            if (!schema.has("probe_voltage_mv") ||
                schema.at("probe_voltage_mv").number() != 3300 ||
                vcc != "3300mV") {
                attempts += ",\"outcome\":\"skipped_voltage\"}";
                continue;
            }
            std::string event = probe_event_v3(request_id, c, module);
            retrode3::JsonV3 parsed = retrode3::parse_json_v3(event);
            if (!parsed.has("system_detected") ||
                parsed.at("system_detected").kind != retrode3::JsonV3::Boolean ||
                !parsed.at("system_detected").boolean) {
                attempts += ",\"outcome\":\"no_match\"}";
                continue;
            }
            ++candidate_count;
            if (candidate_count > 1) candidates += ",";
            candidates += retrode3::NdjsonProtocol::quote(c.id);
            attempts += ",\"outcome\":\"matched\"}";
            selected = c.id;
            selected_event = event;
            if (schema.has("read_voltage_mv") &&
                schema.at("read_voltage_mv").kind == retrode3::JsonV3::Integer &&
                schema.at("read_voltage_mv").integer == 5000 &&
                schema.has("automatic_voltage_switch") &&
                schema.at("automatic_voltage_switch").kind == retrode3::JsonV3::Boolean &&
                schema.at("automatic_voltage_switch").boolean) {
                selected_voltage = "pending";
            } else if (schema.has("read_voltage_mv") &&
                       schema.at("read_voltage_mv").kind == retrode3::JsonV3::Integer &&
                       schema.at("read_voltage_mv").integer == 5000) {
                selected_voltage = "manual_required";
            } else selected_voltage = "ready";
            break;
        } catch (std::exception const &e) {
            attempts += ",\"outcome\":\"failed\",\"reason\":" +
                retrode3::NdjsonProtocol::quote(e.what()) + "}";
        }
    }
    attempts += "]";
    candidates += "]";
    if (read_slot_attribute(slot, "sense") != "active") {
        stream.emit(out + ",\"detection\":\"empty\",\"core\":null,"
                    "\"candidates\":[] ,\"attempts\":" + attempts + "}");
        return;
    }
    if (!selected.empty() && selected_voltage == "pending")
        selected_voltage = apply_voltage(slot, 5000);
    out += ",\"detection\":" +
        retrode3::NdjsonProtocol::quote(selected.empty() ? "manual_required" : "detected")
        + ",\"core\":" + (selected.empty() ? "null" :
            retrode3::NdjsonProtocol::quote(selected))
        + ",\"candidates\":" + candidates
        + ",\"attempts\":" + attempts
        + ",\"voltage_status\":" + retrode3::NdjsonProtocol::quote(selected_voltage)
        + ",\"probe_result\":" + (selected.empty() ? "null" : selected_event)
        + "}";
    stream.emit(out);
}
retrode3_selection_v3 typed_selection_v3(retrode3::WebCommand const &command,
                                        std::string const &schema_id)
{
    retrode3_selection_v3 choice;
    std::memset(&choice, 0, sizeof(choice));
    choice.struct_size = sizeof(choice);
    choice.protocol_version = 1;
    const std::string selected_schema =
        command.has_selection ? command.selection_schema : schema_id;
    if (selected_schema != schema_id)
        throw std::runtime_error("stale or mismatched selection schema");
    std::snprintf(choice.schema_id, sizeof(choice.schema_id), "%s",
                  selected_schema.c_str());
    if (!command.has_selection || command.selection_mode == "auto")
        choice.mode = RETRODE3_SELECT_AUTO_V3;
    else if (command.selection_mode == "candidate")
        choice.mode = RETRODE3_SELECT_CANDIDATE_V3;
    else if (command.selection_mode == "layout")
        choice.mode = RETRODE3_SELECT_LAYOUT_V3;
    else if (command.selection_mode == "override")
        choice.mode = RETRODE3_SELECT_OVERRIDE_V3;
    else throw std::runtime_error("invalid selection mode");
    std::snprintf(choice.candidate_id, sizeof(choice.candidate_id), "%s",
                  command.selection_candidate.c_str());
    std::snprintf(choice.layout_id, sizeof(choice.layout_id), "%s",
                  command.selection_layout.c_str());
    for (std::map<std::string, retrode3::JsonV3>::const_iterator i =
         command.selection_parameters.begin();
         i != command.selection_parameters.end(); ++i) {
        if (choice.parameter_count >= RETRODE3_MAX_PARAMETERS_V3)
            throw std::runtime_error("too many override parameters");
        retrode3_parameter_v3 &p = choice.parameters[choice.parameter_count++];
        std::snprintf(p.id, sizeof(p.id), "%s", i->first.c_str());
        if (i->second.kind == retrode3::JsonV3::Integer) {
            p.type = RETRODE3_PARAMETER_INTEGER_V3;
            p.integer_value = static_cast<std::uint32_t>(i->second.integer);
        } else if (i->second.kind == retrode3::JsonV3::Boolean) {
            p.type = RETRODE3_PARAMETER_BOOLEAN_V3;
            p.integer_value = i->second.boolean ? 1u : 0u;
        } else if (i->second.kind == retrode3::JsonV3::String) {
            p.type = RETRODE3_PARAMETER_STRING_V3;
            std::snprintf(p.string_value, sizeof(p.string_value), "%s",
                          i->second.string.c_str());
        } else throw std::runtime_error("invalid parameter type");
    }
    return choice;
}

void read_rom_v3(retrode3::NdjsonProtocol &stream,
                 retrode3::WebCommand const &command, OpenCore &module)
{
    if (command.has_manual || !command.candidate_id.empty() ||
        !command.layout_id.empty() || command.has_size || !command.mapping.empty())
        throw std::runtime_error("v3 reads require generic selection only");
    const retrode3::JsonV3 schema = capability_schema_v3(module);
    const retrode3_selection_v3 selection =
        typed_selection_v3(command, schema.at("schema_id").text());
    const std::string &output = command.output;
    if (output.empty() || output[0] != '/' || output.size() > 1024 ||
        output.find("/../") != std::string::npos ||
        output.find("/./") != std::string::npos ||
        (output.size() >= 3 && output.compare(output.size()-3, 3, "/..") == 0))
        throw std::runtime_error("invalid absolute output path");
    retrode3_read_plan_v3 plan;
    std::memset(&plan, 0, sizeof(plan));
    plan.struct_size = sizeof(plan);
    char error[256] = {};
    if (module.api3->plan_read(&selection, &plan, error, sizeof(error)))
        throw std::runtime_error(error[0] ? safe_cstr(error, sizeof(error))
                                          : "core rejected read configuration");
    if (plan.struct_size != sizeof(plan) ||
        plan.payload_bytes == 0 || plan.payload_bytes > 128ull * 1024 * 1024 ||
        plan.prefix_bytes > RETRODE3_MAX_PREFIX_V3)
        throw std::runtime_error("invalid v3 read plan");
    const std::size_t total = static_cast<std::size_t>(
        plan.payload_bytes + plan.prefix_bytes);
    if (::access(output.c_str(), F_OK) == 0)
        throw std::runtime_error("output already exists");
    std::string pattern = output + ".partial-XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end());
    temp.push_back('\0');
    int fd = ::mkstemp(temp.data());
    if (fd < 0) throw std::runtime_error("cannot create output file");
    std::FILE *file = ::fdopen(fd, "w+b");
    if (!file) {
        ::close(fd);
        ::unlink(temp.data());
        throw std::runtime_error("cannot open temporary ROM");
    }
    try {
        unsigned char zeros[RETRODE3_MAX_PREFIX_V3] = {};
        if (std::fwrite(zeros, 1, plan.prefix_bytes, file) != plan.prefix_bytes)
            throw std::runtime_error("cannot reserve ROM prefix");
        std::size_t step = total / 100;
        if (step < 4096) step = 4096;
        OutputSink ctx = {file, &stream, command.request_id,
                          total, plan.prefix_bytes, step, step, 0xffffffffu, ""};
        stream.reset_cancel();
        emit_progress(ctx, 0);
        retrode3_host_io_v1 io = {RETRODE3_CORE_OPERATION_ABI_V1,
                                  sizeof(io), &ctx, output_bytes, is_cancelled};
        retrode3_read_result_v3 result;
        std::memset(&result, 0, sizeof(result));
        result.struct_size = sizeof(result);
        std::memset(error, 0, sizeof(error));
        int rc = module.api3->read_rom(&selection, &io, &result,
                                        error, sizeof(error));
        if (rc != 0) {
            if (ctx.failure.empty())
                ctx.failure = stream.cancelled() ? "cancelled" :
                    error[0] ? safe_cstr(error, sizeof(error)) : "core read failed";
            throw std::runtime_error(ctx.failure);
        }
        if (result.struct_size != sizeof(result) || result.prefix_bytes != plan.prefix_bytes ||
            ctx.done != total || result.payload_crc_known > 1 ||
            (result.payload_crc_known && result.payload_crc32 != ~ctx.crc))
            throw std::runtime_error("v3 core read size/checksum mismatch");
        retrode3::JsonV3 details = retrode3::parse_json_v3(
            safe_cstr(result.details_json, sizeof(result.details_json)));
        if (details.kind != retrode3::JsonV3::Object ||
            details.at("read_configuration").kind != retrode3::JsonV3::Object ||
            details.at("identity").kind != retrode3::JsonV3::Object ||
            details.at("verification").kind != retrode3::JsonV3::Object)
            throw std::runtime_error("invalid core result details");
        const retrode3::JsonV3 &verification = details.at("verification");
        std::string status = verification.at("status").text();
        if (status != "verified" && status != "mismatch" &&
            status != "not_found" && status != "ambiguous" &&
            status != "not_checked")
            throw std::runtime_error("unknown verification status");
        if (verification.at("verified").kind != retrode3::JsonV3::Boolean ||
            verification.at("verified").boolean != (status == "verified"))
            throw std::runtime_error("inconsistent core verification");
        if (std::fseek(file, 0, SEEK_SET) ||
            std::fwrite(result.prefix, 1, plan.prefix_bytes, file) != plan.prefix_bytes ||
            std::fflush(file) || std::fseek(file, 0, SEEK_SET))
            throw std::runtime_error("cannot finalize ROM prefix");
        std::uint32_t file_crc = 0xffffffffu;
        std::size_t seen = 0;
        unsigned char buffer[4096];
        while (true) {
            std::size_t count = std::fread(buffer, 1, sizeof(buffer), file);
            for (std::size_t i = 0; i < count; ++i) {
                file_crc ^= buffer[i];
                for (unsigned bit = 0; bit < 8; ++bit)
                    file_crc = (file_crc >> 1) ^
                        ((file_crc & 1u) ? 0xedb88320u : 0u);
            }
            seen += count;
            if (count != sizeof(buffer)) {
                if (std::ferror(file))
                    throw std::runtime_error("cannot verify output file");
                break;
            }
        }
        if (seen != total || ::fsync(::fileno(file)))
            throw std::runtime_error("incomplete v3 output file");
        if (std::fclose(file)) {
            file = NULL;
            throw std::runtime_error("cannot close ROM output");
        }
        file = NULL;
        if (::link(temp.data(), output.c_str()))
            throw std::runtime_error("cannot publish ROM without overwrite");
        ::unlink(temp.data());
        emit_progress(ctx, 100);
        char crc_hex[9], payload_hex[9];
        std::snprintf(crc_hex, sizeof(crc_hex), "%08X", ~file_crc);
        std::snprintf(payload_hex, sizeof(payload_hex), "%08X", ~ctx.crc);
        stream.emit(prefix("result", command.request_id)
            + ",\"ok\":true,\"system\":" +
                retrode3::NdjsonProtocol::quote(command.core)
            + ",\"bytes_written\":" + std::to_string(total)
            + ",\"crc32\":" + retrode3::NdjsonProtocol::quote(crc_hex)
            + ",\"payload_crc32\":" + retrode3::NdjsonProtocol::quote(payload_hex)
            + ",\"output\":" + retrode3::NdjsonProtocol::quote(output)
            + ",\"selection_mode\":" + retrode3::NdjsonProtocol::quote(
                command.has_selection ? command.selection_mode : "auto")
            + ",\"read_configuration\":" +
                retrode3::stringify_json_v3(details.at("read_configuration"))
            + ",\"identity\":" + retrode3::stringify_json_v3(details.at("identity"))
            + ",\"verification\":" +
                retrode3::stringify_json_v3(details.at("verification"))
            + ",\"verified\":" + (status == "verified" ? "true" : "false") + "}");
    } catch (...) {
        if (file) std::fclose(file);
        ::unlink(temp.data());
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
            if (command.action == "probe_slot") {
                probe_slot(stream, command.request_id, command.slot,
                           discover(available, enabled), available, enabled);
                continue;
            }
            if (command.core.empty())
                throw std::runtime_error("missing core identifier");
            std::vector<Core> cores = discover(available, enabled);
            Core core_info = find_usable(cores, command.core);
            if (command.action == "get_capabilities") {
                if (core_info.has_v3) {
                    OpenCore module(available + "/" + core_info.id + ".so");
                    emit_capabilities_v3(stream, command.request_id, core_info, module);
                } else emit_capabilities(stream, command.request_id, core_info);
                continue;
            }
            BusLock lock(enabled);
            OpenCore module(available + "/" + core_info.id + ".so");
            if (module.api3 && (command.action == "probe" ||
                                command.has_selection || !module.api2)) {
                if (command.action == "probe")
                    emit_probe_v3(stream, command.request_id, core_info, module);
                else if (command.action == "read_rom")
                    read_rom_v3(stream, command, module);
            } else if (module.api2) {
                if (command.has_selection)
                    throw std::runtime_error("core does not implement generic selection ABI");
                retrode3_probe_v2 info = fast_probe(module);
                if (command.action == "probe")
                    emit_fast_probe(stream, command.request_id, core_info.id, info);
                else if (command.action == "read_rom")
                    read_rom_v2(stream, command, module, info);
            } else {
                if (command.has_manual || command.has_selection || !command.candidate_id.empty() ||
                    !command.layout_id.empty())
                    throw std::runtime_error("core does not support NES mapper selection");
                retrode3_probe_v1 info = probe(module);
                if (command.action == "probe")
                    emit_probe(stream, command.request_id, core_info.id, info);
                else if (command.action == "read_rom")
                    read_rom(stream, command, module, info);
            }
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
    std::string core_id, action, output, slot_id;
    bool list = false, streamed = false, run_interactive = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list-cores" && !list) list = true;
        else if (arg == "--interactive" && !run_interactive) run_interactive = true;
        else if (arg == "--protocol" && i + 1 < argc &&
                 std::string(argv[i + 1]) == "ndjson-v1") {
            ++i;
            streamed = true;
        } else if (arg == "--core" && i + 1 < argc && core_id.empty()) {
            core_id = argv[++i];
        } else if (arg == "--slot" && i + 1 < argc && slot_id.empty()) {
            slot_id = argv[++i];
        } else if (arg == "--action" && i + 1 < argc && action.empty()) {
            action = argv[++i];
            if (action == "read-rom") action = "read_rom";
            if (action == "probe-slot") action = "probe_slot";
        } else if (arg == "--output" && i + 1 < argc && output.empty()) {
            output = argv[++i];
        }
#ifdef RETRODE3_TESTING
        else if (arg == "--available-dir" && i + 1 < argc)
            available = argv[++i];
        else if (arg == "--enabled-dir" && i + 1 < argc)
            enabled = argv[++i];
#endif
        else {
            std::fprintf(stderr,
                "usage: retrode3-cli --list-cores | --protocol ndjson-v1 "
                "--interactive | --protocol ndjson-v1 --core ID "
                "--action probe|read-rom|probe-slot [--slot ID] [--output FILE]\n");
            return 2;
        }
    }
    unsigned mode_count = (list ? 1u : 0u) + (run_interactive ? 1u : 0u)
                        + (!action.empty() ? 1u : 0u);
    if (mode_count != 1 || ((run_interactive || !action.empty()) && !streamed) ||
        ((list || run_interactive) && (!core_id.empty() || !slot_id.empty() || !output.empty())) ||
        (!action.empty() && ((action == "probe_slot" ?
                                (!core_id.empty() || !is_slug(slot_id)) :
                                (!is_slug(core_id) || !slot_id.empty())) ||
                             (action != "probe" && action != "read_rom" && action != "probe_slot") ||
                             (action == "read_rom" && output.empty()) ||
                             ((action == "probe" || action == "probe_slot") && !output.empty())))) {
        std::fprintf(stderr, "invalid Retrode3 command mode/options\n");
        return 2;
    }
    try {
        if (list) {
            retrode3::NdjsonProtocol(false).emit(to_json(discover(available, enabled)));
        } else if (run_interactive) {
            interactive(available, enabled);
        } else {
            retrode3::NdjsonProtocol stream(false);
            try {
                std::vector<Core> installed = discover(available, enabled);
                if (action == "probe_slot") {
                    probe_slot(stream, "cli", slot_id, installed, available, enabled);
                    return 0;
                }
                Core const &c = find_usable(installed, core_id);
                BusLock lock(enabled);
                OpenCore module(available + "/" + c.id + ".so");
                if (module.api3 && !module.api2) {
                    if (action == "probe") {
                        emit_probe_v3(stream, "cli", c, module);
                    } else {
                        retrode3::WebCommand command;
                        command.action = "read_rom";
                        command.core = c.id;
                        command.request_id = "cli";
                        command.output = output;
                        read_rom_v3(stream, command, module);
                    }
                } else if (module.api2) {
                    retrode3_probe_v2 info = fast_probe(module);
                    if (action == "probe") {
                        emit_fast_probe(stream, "cli", c.id, info);
                    } else {
                        retrode3::WebCommand command;
                        command.action = "read_rom";
                        command.core = c.id;
                        command.request_id = "cli";
                        command.output = output;
                        read_rom_v2(stream, command, module, info);
                    }
                } else {
                    retrode3_probe_v1 info = probe(module);
                    if (action == "probe") {
                        emit_probe(stream, "cli", c.id, info);
                    } else {
                        retrode3::WebCommand command;
                        command.action = "read_rom";
                        command.core = c.id;
                        command.request_id = "cli";
                        command.output = output;
                        read_rom(stream, command, module, info);
                    }
                }
            } catch (std::exception const &e) {
                std::string why(e.what());
                emit_error(stream, "cli", why == "cancelled" ? "cancelled"
                                                            : "operation_failed", why);
                return 2;
            }
        }
        return 0;
    } catch (std::exception const &e) {
        std::fprintf(stderr, "Retrode3 host failed: %s\n", e.what());
        return 2;
    }
}
