# Board files

A board file gives the SDK the chip variant and, optionally, a default JTAG
pinout — so `-DPICO_BOARD=<name>` is all someone with that board has to pass.

| File | Board |
|---|---|
| `rp2350b.h` | any RP2350B (QFN-80, 48 GPIO), no pinout |
| `zen.h` | Vicharak Zen — RP2350B + Efinix Trion T4F81 |

A stock Pico or Pico 2 needs nothing here; the SDK ships those headers.

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
