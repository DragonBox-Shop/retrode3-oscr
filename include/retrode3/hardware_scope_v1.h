#ifndef RETRODE3_HARDWARE_SCOPE_V1_H
#define RETRODE3_HARDWARE_SCOPE_V1_H

// Generic, scoped hardware configuration for trusted v3 core capabilities.
// Only the core describes requirements. The shared host knows neither console
// mappers nor cartridge-specific control logic. The existing BusLock MUST be
// acquired before constructing this scope and held until its destruction.
#include "retrode3/json_v3.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace retrode3 {

class HardwareScopeV1 {
    struct Previous {
        std::string path;
        std::string value;
        bool voltage;
    };
    std::vector<Previous> previous_;
    struct EnvPrevious {
        std::string name;
        std::string value;
        bool existed;
    };
    std::vector<EnvPrevious> env_previous_;
    bool restoring_;

    static std::string slot_root() {
#ifdef RETRODE3_TESTING
        const char *root = std::getenv("RETRODE3_TEST_SLOT_ROOT");
        if (root && *root) return root;
#endif
        return "/sys/class/retrode3";
    }
    static std::string parameter_root() {
#ifdef RETRODE3_TESTING
        const char *root = std::getenv("RETRODE3_TEST_KERNEL_PARAM_ROOT");
        if (root && *root) return root;
#endif
        return "/sys/module/retrode3/parameters";
    }
    static std::string read_value(const std::string &path) {
        std::ifstream in(path.c_str());
        std::string value;
        if (!in || !std::getline(in, value))
            throw std::runtime_error("hardware state unavailable: " + path);
        return value;
    }
    static void write_value(const std::string &path, const std::string &value) {
        const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0)
            throw std::runtime_error("cannot change hardware state: " + path);
        const std::string data = value + "\n";
        const ssize_t n = ::write(fd, data.data(), data.size());
        const int close_rc = ::close(fd);
        if (n != static_cast<ssize_t>(data.size()) || close_rc != 0)
            throw std::runtime_error("incomplete hardware state write: " + path);
    }
    static void write_voltage(const std::string &path, const std::string &mv) {
#ifdef RETRODE3_TESTING
        // Regular-file fake sysfs stores displayed values, not kernfs setters.
        const char *root = std::getenv("RETRODE3_TEST_SLOT_ROOT");
        if (root && *root) { write_value(path, mv + "mV"); return; }
#endif
        write_value(path, mv);
    }
    static bool identifier(const std::string &s) {
        if (s.empty() || s.size() > 64) return false;
        for (std::size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  (c == '_'))) return false;
        }
        return true;
    }
    static bool state(const std::string &s) {
        if (s == "Y" || s == "1") return true;
        if (s == "N" || s == "0") return false;
        throw std::runtime_error("unexpected kernel parameter state");
    }
    void update_environment(const std::string &name, const std::string &value) {
        if (name.size() < 10 || name.compare(0, 9, "RETRODE3_") != 0 ||
            name.size() > 64 || value.empty() || value.size() > 127)
            throw std::runtime_error("invalid core environment profile");
        for (std::size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_'))
                throw std::runtime_error("invalid core environment key");
        }
        for (std::size_t i = 0; i < value.size(); ++i) {
            char c = value[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-'))
                throw std::runtime_error("invalid core environment value");
        }
        const char *before = std::getenv(name.c_str());
        env_previous_.push_back(EnvPrevious{name, before ? before : "",
                                           before != NULL});
        if (::setenv(name.c_str(), value.c_str(), 1) != 0)
            throw std::runtime_error("cannot apply core environment profile");
    }
    void update_parameter(const std::string &name, bool desired) {
        if (!identifier(name))
            throw std::runtime_error("invalid hardware parameter identifier");
        const std::string path = parameter_root() + "/" + name;
        const std::string original = read_value(path);
        if (state(original) == desired) return;
        previous_.push_back(Previous{path, original, false});
        write_value(path, desired ? "1" : "0");
        if (state(read_value(path)) != desired)
            throw std::runtime_error("kernel parameter readback mismatch");
    }
    void update_voltage(const std::string &slot, unsigned mv) {
        if (mv != 3300 && mv != 5000)
            throw std::runtime_error("unsupported hardware profile voltage");
        const std::string path = slot_root() + "/slot-" + slot + "/vcc";
        const std::string original = read_value(path);
        const std::string desired = std::to_string(mv) + "mV";
        if (original == desired) return;
        if (original != "3300mV" && original != "5000mV")
            throw std::runtime_error("slot has no switchable voltage");
        if (read_value(slot_root() + "/slot-" + slot + "/sense") != "active")
            throw std::runtime_error("cartridge removed before voltage switch");
        previous_.push_back(Previous{path, original.substr(0, 4), true});
        write_voltage(path, std::to_string(mv));
        if (read_value(path) != desired)
            throw std::runtime_error("slot voltage readback mismatch");
    }
    void restore() {
        if (restoring_) return;
        restoring_ = true;
        while (!env_previous_.empty()) {
            const EnvPrevious entry = env_previous_.back();
            env_previous_.pop_back();
            const int rc = entry.existed ?
                ::setenv(entry.name.c_str(), entry.value.c_str(), 1) :
                ::unsetenv(entry.name.c_str());
            if (rc != 0)
                std::fprintf(stderr, "Retrode3 environment restore failed: %s\n",
                             entry.name.c_str());
        }
        // Reverse order: restore voltage before reducing hardware permissions.
        while (!previous_.empty()) {
            const Previous entry = previous_.back();
            previous_.pop_back();
            try {
                if (entry.voltage) write_voltage(entry.path, entry.value);
                else write_value(entry.path, entry.value);
                const std::string actual = read_value(entry.path);
                if (entry.voltage) {
                    if (actual != entry.value + "mV")
                        throw std::runtime_error("voltage restoration mismatch");
                } else if (state(actual) != state(entry.value)) {
                    throw std::runtime_error("parameter restoration mismatch");
                }
            } catch (std::exception const &e) {
                std::fprintf(stderr, "Retrode3 hardware restore failure: %s (%s)\n",
                             entry.path.c_str(), e.what());
            }
        }
    }
public:
    HardwareScopeV1(const JsonV3 &schema, const std::string &slot,
                    const char *phase)
        : restoring_(false) {
#ifdef RETRODE3_TESTING
        // Existing offline fixtures have no /sys module or voltage controls.
        // Explicitly opted-in scope tests provide fake sysfs roots and
        // exercise the exact same transition/rollback implementation.
        const char *test_scope = std::getenv("RETRODE3_TEST_HARDWARE_SCOPE");
        if (!test_scope || std::strcmp(test_scope, "1") != 0) return;
#endif
        if (!schema.has("hardware_access")) return; // legacy v3 compatibility
        if (!identifier(slot) || !phase ||
            (std::strcmp(phase, "probe") && std::strcmp(phase, "read")))
            throw std::runtime_error("invalid hardware access scope");
        const JsonV3 &access = schema.at("hardware_access");
        if (access.kind != JsonV3::Object ||
            access.at("version").number() != 1)
            throw std::runtime_error("unknown hardware access profile");
        const JsonV3 &profile = access.at(phase);
        if (profile.kind != JsonV3::Object)
            throw std::runtime_error("invalid hardware access phase");
        try {
            if (profile.has("module_parameters")) {
                const JsonV3 &parameters = profile.at("module_parameters");
                if (parameters.kind != JsonV3::Object ||
                    parameters.object.size() > 16)
                    throw std::runtime_error("invalid module parameter profile");
                for (std::map<std::string, JsonV3>::const_iterator it =
                         parameters.object.begin(); it != parameters.object.end(); ++it) {
                    if (it->second.kind != JsonV3::Boolean)
                        throw std::runtime_error("hardware parameter must be boolean");
                    update_parameter(it->first, it->second.boolean);
                }
            }
            if (profile.has("voltage_mv")) {
                const std::uint64_t mv = profile.at("voltage_mv").number();
                if (mv != 3300 && mv != 5000)
                    throw std::runtime_error("unknown hardware profile voltage");
                update_voltage(slot, static_cast<unsigned>(mv));
            }
            if (profile.has("environment")) {
                const JsonV3 &env = profile.at("environment");
                if (env.kind != JsonV3::Object || env.object.size() > 16)
                    throw std::runtime_error("invalid core environment profile");
                for (std::map<std::string, JsonV3>::const_iterator it =
                         env.object.begin(); it != env.object.end(); ++it) {
                    if (it->second.kind != JsonV3::String)
                        throw std::runtime_error("core environment requires strings");
                    update_environment(it->first, it->second.string);
                }
            }
        } catch (...) {
            restore(); // Failed constructor has no destructor.
            throw;
        }
    }
    ~HardwareScopeV1() { restore(); }
private:
    HardwareScopeV1(const HardwareScopeV1 &);
    HardwareScopeV1 &operator=(const HardwareScopeV1 &);
};

} // namespace retrode3

#endif
