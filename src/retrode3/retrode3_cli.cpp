// Discovery-only native CLI milestone. There are no cartridge operations
// here: the common NDJSON runtime and the module operation ABI come next.
// Never execute a core for an untrusted or user-writable installation path.
#include "retrode3/core_descriptor_v1.h"
#include "retrode3/ndjson_protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
struct Core {
    std::string id;
    std::string name;
    std::string version;
    std::string slot;
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

bool has_suffix(std::string const &s, std::string const &suffix)
{
    return s.size() > suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void note(std::string const &name, std::string const &why)
{
    std::fprintf(stderr, "Ignoring Retrode3 core %s: %s\n",
                 name.c_str(), why.c_str());
}

bool secure(const struct stat &st)
{
    return st.st_uid == 0 && (st.st_mode & 022) == 0;
}

std::vector<Core> discover(std::string const &directory)
{
    struct stat dir_stat;
    if (::lstat(directory.c_str(), &dir_stat) != 0) {
        if (errno == ENOENT) return std::vector<Core>();
        throw std::runtime_error("cannot inspect core directory");
    }
    if (!S_ISDIR(dir_stat.st_mode))
        throw std::runtime_error("core installation path must be a real directory");
#ifndef RETRODE3_TESTING
    if (!secure(dir_stat))
        throw std::runtime_error("core directory is not root-owned and write-protected");
#endif
    DIR *dir = ::opendir(directory.c_str());
    if (!dir) throw std::runtime_error("cannot open core directory");

    std::vector<Core> found;
    try {
        struct dirent *entry;
        while ((entry = ::readdir(dir)) != NULL) {
            std::string filename(entry->d_name);
            if (!has_suffix(filename, ".so")) continue;
            std::string id = filename.substr(0, filename.size() - 3);
            if (!is_slug(id)) { note(filename, "invalid filename"); continue; }
            std::string path = directory + "/" + filename;
            struct stat st;
            if (::lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                note(filename, "not a regular file (symlinks are forbidden)");
                continue;
            }
#ifndef RETRODE3_TESTING
            if (!secure(st)) {
                note(filename, "not root-owned and write-protected");
                continue;
            }
#endif
            // Only trusted root-installed modules reach dlopen. The normal
            // binary cannot accept a caller-supplied library search path.
            void *handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) { note(filename, "cannot load shared object"); continue; }
            try {
                void *symbol = ::dlsym(handle, "retrode3_core_describe_v1");
                if (!symbol) {
                    note(filename, "missing discovery ABI symbol");
                } else {
                    retrode3_core_describe_v1_fn describe =
                        reinterpret_cast<retrode3_core_describe_v1_fn>(symbol);
                    const retrode3_core_descriptor_v1 *desc = describe();
                    if (!desc || desc->abi_version != RETRODE3_CORE_DISCOVERY_ABI_V1 ||
                        desc->struct_size < sizeof(retrode3_core_descriptor_v1) ||
                        !desc->id || !desc->name || !desc->version || !desc->slot) {
                        note(filename, "incompatible discovery ABI");
                    } else if (!is_slug(desc->id) || id != desc->id ||
                               !is_slug(desc->slot) ||
                               std::strlen(desc->name) > 128 ||
                               std::strlen(desc->version) > 64 ||
                               desc->name[0] == '\0' || desc->version[0] == '\0') {
                        note(filename, "invalid identity or metadata");
                    } else {
                        Core c = {desc->id, desc->name, desc->version, desc->slot};
                        found.push_back(c);
                    }
                }
            } catch (...) {
                ::dlclose(handle);
                throw;
            }
            ::dlclose(handle);
        }
        ::closedir(dir);
    } catch (...) {
        ::closedir(dir);
        throw;
    }
    std::sort(found.begin(), found.end(),
              [](Core const &a, Core const &b) { return a.id < b.id; });
    return found;
}

std::string to_json(std::vector<Core> const &cores)
{
    std::string json = "{\"protocol\":\"retrode3-ndjson\",\"version\":1,"
        "\"event\":\"cores\",\"request_id\":\"cli\","
        "\"discovery_abi\":1,\"cores\":[";
    for (std::size_t i = 0; i < cores.size(); ++i) {
        Core const &c = cores[i];
        if (i) json += ",";
        json += "{\"id\":" + retrode3::NdjsonProtocol::quote(c.id)
              + ",\"name\":" + retrode3::NdjsonProtocol::quote(c.name)
              + ",\"version\":" + retrode3::NdjsonProtocol::quote(c.version)
              + ",\"slot\":" + retrode3::NdjsonProtocol::quote(c.slot)
              + ",\"installed\":true,\"status\":\"discovered\","
                "\"usable\":false,\"actions\":[]}";
    }
    return json + "]}";
}
}

int main(int argc, char **argv)
{
    std::string directory = "/usr/lib/retrode3/cores";
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list-cores" && !list) {
            list = true;
        } else if (arg == "--protocol" && i + 1 < argc &&
                   std::string(argv[i + 1]) == "ndjson-v1") {
            ++i;
#ifdef RETRODE3_TESTING
        } else if (arg == "--core-dir" && i + 1 < argc) {
            directory = argv[++i];
#endif
        } else {
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
        retrode3::NdjsonProtocol(false).emit(to_json(discover(directory)));
        return 0;
    } catch (std::exception const &e) {
        std::fprintf(stderr, "Retrode3 core discovery failed: %s\n", e.what());
        return 2;
    }
}
