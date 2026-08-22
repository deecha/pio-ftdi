# Board files

A board file gives the SDK the chip variant and, optionally, a default JTAG
pinout — so `-DPICO_BOARD=<name>` is all someone with that board has to pass.

| File | Board | Pins (TCK/TDI/TDO/TMS) | Extra flag |
|---|---|---|---|
| `pico_jtag.h` | Raspberry Pi Pico (RP2040) | GPIO 1/2/3/4 | `-DRP2040=1` |
| `pico2_jtag.h` | Raspberry Pi Pico 2 (RP2350A) | GPIO 1/2/3/4 | — |
| `zen.h` | Vicharak Zen (RP2350B + Trion T4F81) | GPIO 30/31/32/33 | — |
| `rp2350b.h` | any RP2350B, no pinout | — | — |

The names deliberately do not match the SDK's own `pico` and `pico2`: this
directory is searched **before** the SDK's `boards/`, so reusing those names
would shadow the real headers and lose everything they define.

## Adding one

```c
#ifndef _BOARDS_MYBOARD_H
#define _BOARDS_MYBOARD_H

#define PICO_RP2350A 0          // only for RP2350B; omit on RP2040/RP2350A

#ifndef PIN_TCK
#define PIN_TCK   30
#define PIN_TDI   31
#define PIN_TDO   32
#define PIN_TMS   33
#endif

#endif
```

Then `cmake -DPICO_BOARD=myboard ..`.

Two things matter:

**Keep the `#ifndef` guards.** The SDK includes this header via `pico.h`, which
`board_config.h` pulls in before its own defaults — so these win over
`board_config.h` but still lose to `-DJTAG_PINS`, which is the precedence you
want. Drop the guards and `-DJTAG_PINS` becomes a redefinition error.

**`PICO_RP2350A 0` is required for GPIO30 and above.** It sets
`NUM_BANK0_GPIOS`, which decides which pins exist at all. Without it the build
rejects them.
