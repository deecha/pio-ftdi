// Generic RP2350B board header.
//
// The SDK needs to know this is the B variant (QFN-80, 48 GPIO) rather than
// the A (QFN-60, 30 GPIO), because that sets NUM_BANK0_GPIOS and therefore
// which pins exist at all. Without it, GPIO30+ is rejected.
//
// Adjust the flash settings below if your board differs; they do not affect
// the JTAG firmware, which never touches flash at runtime.
#ifndef _BOARDS_RP2350B_H
#define _BOARDS_RP2350B_H

#define PICO_RP2350A 0          // 0 => B variant, 48 GPIO

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif
#ifndef PICO_BOOT_STAGE2_CHOOSE_W25Q080
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#endif
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#endif
