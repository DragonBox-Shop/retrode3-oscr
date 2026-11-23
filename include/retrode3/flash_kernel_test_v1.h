#ifndef RETRODE3_FLASH_KERNEL_TEST_V1_H
#define RETRODE3_FLASH_KERNEL_TEST_V1_H

/* Exact experimental ioctl wire format from kernel feature/md-flash-cfi-7.3,
 * retrode3_flash_ioctl.h. Keep in sync until promoted to stable UAPI.
 * _IOWR MUST be evaluated with target MIPS headers, not an x86 constant. */
#include <linux/ioctl.h>
#include <linux/types.h>

#define RETRODE3_FLASH_CFI_ABI_V1 1U
#define RETRODE3_FLASH_CFI_QUERY_BYTES 128U
#define RETRODE3_FLASH_CHUNK_BYTES 512U

struct retrode3_flash_cfi_query_v1 {
    __u32 abi_version;
    __u32 flags;
    __u32 bus_width_bits;
    __u32 query_bytes;
    __u8 data[RETRODE3_FLASH_CFI_QUERY_BYTES];
};
struct retrode3_flash_id_v1 {
    __u32 abi_version;
    __u32 flags;
    __u32 manufacturer_id;
    __u32 device_id;
    __u32 capacity_bytes;
    __u32 command_set;
    __u32 sector_count;
};
struct retrode3_flash_erase_v1 {
    __u32 abi_version;
    __u32 flags;
    __u32 offset_bytes;
};
struct retrode3_flash_program_v1 {
    __u32 abi_version;
    __u32 flags;
    __u32 offset_bytes;
    __u32 length;
    __u8 data[RETRODE3_FLASH_CHUNK_BYTES];
};
#define RETRODE3_FLASH_IOC_CFI_QUERY _IOWR('R', 0xf0, struct retrode3_flash_cfi_query_v1)
#define RETRODE3_FLASH_IOC_IDENTIFY  _IOWR('R', 0xf1, struct retrode3_flash_id_v1)
#define RETRODE3_FLASH_IOC_ERASE     _IOW('R', 0xf2, struct retrode3_flash_erase_v1)
#define RETRODE3_FLASH_IOC_PROGRAM   _IOW('R', 0xf3, struct retrode3_flash_program_v1)

#endif
