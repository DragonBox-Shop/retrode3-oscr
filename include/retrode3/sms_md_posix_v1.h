#ifndef RETRODE3_SMS_MD_POSIX_V1_H
#define RETRODE3_SMS_MD_POSIX_V1_H
#include "retrode3/sms_rom_core_v1.h"
namespace retrode3 { namespace sms {

typedef int (*CancelFn)(void *context);
bool sms_experiment_enabled();
bool sms_voltage_is_5v();

class SmsMdPosixBus : public Bus {
    int fd_;
    CancelFn cancel_;
    void *context_;
    SmsMdPosixBus(const SmsMdPosixBus &);
    SmsMdPosixBus &operator=(const SmsMdPosixBus &);
public:
    explicit SmsMdPosixBus(CancelFn cancel = NULL, void *context = NULL);
    virtual ~SmsMdPosixBus();
    virtual bool approved() const;
    virtual bool read(std::uint16_t address, std::uint8_t *bytes, std::size_t count);
    virtual bool begin_sega_banked();
    virtual bool select_page2(std::uint8_t bank);
    virtual bool end_sega_banked();
    virtual bool cancelled() const;
};
} }
#endif
