// Offline regression for the generic v3 hardware phase state machine.
// No real /sys, cartridge or kernel module is touched.
#include "retrode3/hardware_scope_v1.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static void save(const std::string &path, const char *value)
{
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(::write(fd, value, std::strlen(value)) ==
           static_cast<ssize_t>(std::strlen(value)));
    assert(::close(fd) == 0);
}
static std::string get(const std::string &path)
{
    std::ifstream in(path.c_str());
    std::string value;
    assert(std::getline(in, value));
    return value;
}
int main()
{
    char directory[] = "/tmp/retrode3-hardware-scope-XXXXXX";
    char *root = ::mkdtemp(directory);
    assert(root);
    const std::string base(root);
    const std::string slots = base + "/slots";
    const std::string params = base + "/params";
    const std::string md = slots + "/slot-md";
    assert(::mkdir(slots.c_str(), 0700) == 0);
    assert(::mkdir(params.c_str(), 0700) == 0);
    assert(::mkdir(md.c_str(), 0700) == 0);
    const std::string vcc = md + "/vcc";
    const std::string sense = md + "/sense";
    const std::string hiz = params + "/legacy_bridge_hiz_test";
    const std::string mapper = params + "/sms_mapper_write_test";
    save(vcc, "3300mV\n");
    save(sense, "active\n");
    save(hiz, "N\n");
    save(mapper, "N\n");
    assert(::setenv("RETRODE3_TEST_HARDWARE_SCOPE", "1", 1) == 0);
    assert(::setenv("RETRODE3_TEST_SLOT_ROOT", slots.c_str(), 1) == 0);
    assert(::setenv("RETRODE3_TEST_KERNEL_PARAM_ROOT", params.c_str(), 1) == 0);
    assert(::unsetenv("RETRODE3_LAB_TEST") == 0);

    const retrode3::JsonV3 schema = retrode3::parse_json_v3(
        "{\"hardware_access\":{\"version\":1,"
        "\"probe\":{\"voltage_mv\":3300,"
        "\"module_parameters\":{\"legacy_bridge_hiz_test\":true,"
        "\"sms_mapper_write_test\":false}},"
        "\"read\":{\"voltage_mv\":5000,"
        "\"module_parameters\":{\"legacy_bridge_hiz_test\":true,"
        "\"sms_mapper_write_test\":true},"
        "\"environment\":{\"RETRODE3_LAB_TEST\":\"enabled\"}}}}");
    {
        retrode3::HardwareScopeV1 scope(schema, "md", "probe");
        assert(get(vcc) == "3300mV");
        assert(get(hiz) == "1");
        assert(get(mapper) == "N");
    }
    assert(get(vcc) == "3300mV");
    assert(get(hiz) == "N");
    {
        retrode3::HardwareScopeV1 scope(schema, "md", "read");
        assert(get(vcc) == "5000mV");
        assert(get(hiz) == "1");
        assert(get(mapper) == "1");
        assert(std::strcmp(std::getenv("RETRODE3_LAB_TEST"), "enabled") == 0);
    }
    assert(get(vcc) == "3300mV");
    assert(get(hiz) == "N");
    assert(get(mapper) == "N");
    assert(!std::getenv("RETRODE3_LAB_TEST"));

    // On rejected voltage switch, constructor must restore BOTH kernel gates.
    save(sense, "empty\n");
    try {
        retrode3::HardwareScopeV1 scope(schema, "md", "read");
        assert(false && "expected missing-cartridge refusal");
    } catch (std::runtime_error const &) {}
    assert(get(vcc) == "3300mV");
    assert(get(hiz) == "N");
    assert(get(mapper) == "N");
    assert(!std::getenv("RETRODE3_LAB_TEST"));

    assert(::unlink(vcc.c_str()) == 0);
    assert(::unlink(sense.c_str()) == 0);
    assert(::unlink(hiz.c_str()) == 0);
    assert(::unlink(mapper.c_str()) == 0);
    assert(::rmdir(md.c_str()) == 0);
    assert(::rmdir(slots.c_str()) == 0);
    assert(::rmdir(params.c_str()) == 0);
    assert(::rmdir(base.c_str()) == 0);
    return 0;
}
