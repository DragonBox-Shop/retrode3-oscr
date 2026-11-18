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
}

int main(int argc, char **argv)
{
    std::string available = "/usr/lib/retrode3/cores-available";
    std::string enabled = "/etc/retrode3/cores-enabled";
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list-cores" && !list) list = true;
        else if (arg == "--protocol" && i + 1 < argc &&
                 std::string(argv[i + 1]) == "ndjson-v1") ++i;
#ifdef RETRODE3_TESTING
        else if (arg == "--available-dir" && i + 1 < argc)
            available = argv[++i];
        else if (arg == "--enabled-dir" && i + 1 < argc)
            enabled = argv[++i];
#endif
        else {
            std::fprintf(stderr,
                "usage: retrode3-cli --list-cores [--protocol ndjson-v1]\n");
            return 2;
        }
    }
    if (!list) {
        std::fprintf(stderr, "usage: retrode3-cli --list-cores\n");
        return 2;
    }
    try {
        retrode3::NdjsonProtocol(false).emit(to_json(discover(available, enabled)));
        return 0;
    } catch (std::exception const &e) {
        std::fprintf(stderr, "Retrode3 core discovery failed: %s\n", e.what());
        return 2;
    }
}
