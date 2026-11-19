// Privileged offline core enable/disable helper. Never accepts a library path.
// This helper only creates/removes a symlink, never loads cartridge code.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool slug(std::string const &s)
{
    if (s.empty() || s.size() > 48) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_')) return false;
    }
    return true;
}
#ifndef RETRODE3_TESTING
bool trusted(struct stat const &st)
{
    return st.st_uid == 0 && (st.st_mode & 022) == 0;
}
#endif
bool safe_dir(std::string const &path)
{
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return false;
#ifndef RETRODE3_TESTING
    if (!trusted(st)) return false;
#endif
    return true;
}
bool safe_file(std::string const &path)
{
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
#ifndef RETRODE3_TESTING
    if (!trusted(st)) return false;
#endif
    return true;
}
bool read_link(std::string const &path, std::string &target)
{
    char buffer[4096];
    ssize_t n = ::readlink(path.c_str(), buffer, sizeof(buffer));
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buffer)) return false;
    target.assign(buffer, static_cast<std::size_t>(n));
    return true;
}
void toggle(std::string const &action, std::string const &id,
            std::string const &available, std::string const &enabled)
{
    if (!slug(id)) throw std::runtime_error("invalid core identifier");
    if (action != "enable" && action != "disable")
        throw std::runtime_error("expected enable or disable");
#ifndef RETRODE3_TESTING
    if (::geteuid() != 0)
        throw std::runtime_error("core activation requires root privileges");
#endif
    if (!safe_dir(available))
        throw std::runtime_error("untrusted or missing cores-available directory");
    if (!safe_dir(enabled))
        throw std::runtime_error("untrusted or missing cores-enabled directory");
    std::string target = available + "/" + id + ".so";
    std::string manifest = available + "/" + id + ".core";
    std::string link = enabled + "/" + id + ".so";
    if (action == "enable" &&
        (!safe_file(target) || !safe_file(manifest)))
        throw std::runtime_error("core is not installed or is untrusted");

    struct stat st;
    if (::lstat(link.c_str(), &st) == 0) {
        std::string actual;
        if (!S_ISLNK(st.st_mode) || !read_link(link, actual) ||
            actual != target)
            throw std::runtime_error("existing enablement path is invalid");
#ifndef RETRODE3_TESTING
        if (st.st_uid != 0)
            throw std::runtime_error("enablement symlink must be root-owned");
#endif
        if (action == "disable" && ::unlink(link.c_str()) != 0)
            throw std::runtime_error("cannot disable core");
    } else if (errno != ENOENT) {
        throw std::runtime_error("cannot inspect enablement path");
    } else if (action == "enable" &&
               ::symlink(target.c_str(), link.c_str()) != 0) {
        throw std::runtime_error("cannot enable core");
    }
    std::printf("%s: %s\n", id.c_str(),
                action == "enable" ? "enabled" : "disabled");
}
}
int main(int argc, char **argv)
{
    std::string available = "/usr/lib/retrode3/cores-available";
    std::string enabled = "/etc/retrode3/cores-enabled";
    if (argc < 3) {
        std::fprintf(stderr, "usage: retrode3-corectl enable|disable <core-id>\n");
        return 2;
    }
#ifdef RETRODE3_TESTING
    if (argc == 7 && std::string(argv[3]) == "--available-dir" &&
        std::string(argv[5]) == "--enabled-dir") {
        available = argv[4];
        enabled = argv[6];
    } else if (argc != 3) {
        std::fprintf(stderr, "invalid arguments\n");
        return 2;
    }
#else
    if (argc != 3) {
        std::fprintf(stderr, "invalid arguments\n");
        return 2;
    }
#endif
    try {
        toggle(argv[1], argv[2], available, enabled);
        return 0;
    } catch (std::exception const &e) {
        std::fprintf(stderr, "Retrode3 core control: %s\n", e.what());
        return 2;
    }
}
