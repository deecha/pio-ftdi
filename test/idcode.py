#!/usr/bin/env python3
"""Read the Trion IDCODE with hand-written MPSSE. No OpenOCD.

Drives the TAP explicitly so every byte on the wire is visible:
  reset -> Shift-DR -> shift 32 bits -> print.

After TAP reset the IDCODE instruction is loaded automatically, so Shift-DR
gives the IDCODE directly. Bit 0 of a valid JTAG IDCODE is always 1.

Run this against the Zen board, then against your working external FTDI
adapter and compare. Same script, same wire sequence, so any difference is
in the adapter.

    python3 test/idcode.py
"""
import sys
import usb.core
import usb.util

VID, PID, EP_IN, EP_OUT, IFACE = 0x0403, 0x6014, 0x81, 0x02, 0

def flush(dev):
    try:
        while True:
            dev.read(EP_IN, 64, 100)
    except usb.core.USBError:
        pass

def xfer(dev, cmd, want):
    """Send MPSSE bytes, return `want` payload bytes (status prefix stripped)."""
    dev.write(EP_OUT, bytes(cmd) + b"\x87", 1000)
    got = b""
    for _ in range(20):
        try:
            r = bytes(dev.read(EP_IN, 64, 300))
        except usb.core.USBError:
            continue
        if len(r) > 2:
            got += r[2:]
        if len(got) >= want:
            break
    return got

def main():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("device not found")
    try:
        if dev.is_kernel_driver_active(IFACE):
            dev.detach_kernel_driver(IFACE)
    except (NotImplementedError, usb.core.USBError):
        pass

    dev.set_configuration()
    usb.util.claim_interface(dev, IFACE)
    dev.ctrl_transfer(0x40, 0x00, 0x0000, 1, None, 1000)
    dev.ctrl_transfer(0x40, 0x0B, 0x020B, 1, None, 1000)
    dev.ctrl_transfer(0x40, 0x09, 2, 1, None, 1000)
    flush(dev)

    # sync check
    r = xfer(dev, [0xAB], 2)
    print(f"sync 0xAB -> {r.hex(' ')}  {'OK' if r[:2] == b'\\xfa\\xab' else 'FAIL'}")

    setup = [
        0x8A,                    # 60 MHz base (no divide-by-5)
        0x86, 0x1D, 0x00,        # divisor 29 -> 1 MHz TCK
        0x85,                    # loopback off
        0x80, 0x08, 0x0B,        # ADBUS: TMS high, TCK/TDI/TMS outputs
    ]
    dev.write(EP_OUT, bytes(setup), 1000)
    flush(dev)

    # TAP reset: 6 clocks with TMS high -> Test-Logic-Reset
    # then TMS 0,1,0,0 -> Run-Test/Idle -> Select-DR -> Capture-DR -> Shift-DR
    tap = [
        0x4B, 0x05, 0x3F,        # 6 bits, TMS = 111111
        0x4B, 0x03, 0x02,        # 4 bits, TMS = 0,1,0,0 (LSB first)
    ]
    dev.write(EP_OUT, bytes(tap), 1000)
    flush(dev)

    # shift 32 bits out of DR, reading TDO
    idc = xfer(dev, [0x39, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00], 4)
    print(f"\nraw DR bytes: {idc.hex(' ')}")

    if len(idc) < 4:
        print("!! short read -- adapter returned fewer than 4 bytes")
        return

    v = int.from_bytes(idc[:4], "little")
    print(f"IDCODE = 0x{v:08x}")
    print(f"  bit0    = {v & 1}   (must be 1 for a valid IDCODE)")
    print(f"  manuf   = 0x{(v >> 1) & 0x7FF:03x}")
    print(f"  part    = 0x{(v >> 12) & 0xFFFF:04x}")
    print(f"  version = 0x{(v >> 28) & 0xF:x}")

    if v in (0x00000000, 0xFFFFFFFF):
        print("\n-> TDO never changed. Nothing driving, or shifting is broken.")
    elif not (v & 1):
        print("\n-> bit0 is 0: bit alignment is off by one somewhere.")
    else:
        print("\n-> structurally valid. Compare against the external adapter.")

    usb.util.release_interface(dev, IFACE)

if __name__ == "__main__":
    main()
