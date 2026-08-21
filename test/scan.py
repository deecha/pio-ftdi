#!/usr/bin/env python3
"""Dump the raw JTAG DR bitstream. Works with any FTDI-based adapter.

    python3 test/scan.py                # default 0403:6010
    python3 test/scan.py 0403:6014      # or whatever lsusb shows
    python3 test/scan.py --list         # show candidate FTDI devices

Run against the Zen board AND your working external adapter, then compare the
bit strings. A constant offset between them is an alignment bug; completely
different content is something else.
"""
import sys
import usb.core
import usb.util

EP_IN, EP_OUT, IFACE = 0x81, 0x02, 0
FTDI_PIDS = [0x6001, 0x6010, 0x6011, 0x6014, 0x6015]

def list_devices():
    print("USB devices that look like FTDI adapters:\n")
    found = False
    for d in usb.core.find(find_all=True):
        if d.idVendor == 0x0403 or d.idProduct in FTDI_PIDS:
            try:
                prod = usb.util.get_string(d, d.iProduct) or "?"
            except Exception:
                prod = "?"
            print(f"  {d.idVendor:04x}:{d.idProduct:04x}  {prod}")
            found = True
    if not found:
        print("  none found. Try: lsusb")
    print("\nRe-run as:  python3 test/scan.py VID:PID")

def flush(dev, tries=6):
    for _ in range(tries):
        try:
            dev.read(EP_IN, 64, 20)
        except usb.core.USBError:
            return

def xfer(dev, cmd, want):
    dev.write(EP_OUT, bytes(cmd) + b"\x87", 1000)
    got = b""
    for _ in range(15):
        try:
            r = bytes(dev.read(EP_IN, 64, 150))
        except usb.core.USBError:
            continue
        if len(r) > 2:
            got += r[2:]
        if len(got) >= want:
            break
    return got

def bits_of(data):
    """MPSSE LSB-first: bit 0 of byte 0 is the first bit off the wire."""
    out = ""
    for byte in data:
        for i in range(8):
            out += "1" if (byte >> i) & 1 else "0"
    return out

def tap_reset_to_shift_dr(dev):
    dev.write(EP_OUT, bytes([
        0x4B, 0x05, 0x3F,     # 6 clocks TMS=1 -> Test-Logic-Reset
        0x4B, 0x03, 0x02,     # TMS 0,1,0,0    -> Run-Test/Idle, Select-DR,
    ]), 1000)                 #                   Capture-DR, Shift-DR
    flush(dev)

def main():
    args = [a for a in sys.argv[1:]]
    if "--list" in args:
        list_devices()
        return

    vid, pid = 0x0403, 0x6014
    for a in args:
        if ":" in a:
            v, p = a.split(":")
            vid, pid = int(v, 16), int(p, 16)

    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        print(f"{vid:04x}:{pid:04x} not found.\n")
        list_devices()
        return

    try:
        if dev.is_kernel_driver_active(IFACE):
            dev.detach_kernel_driver(IFACE)
    except (NotImplementedError, usb.core.USBError):
        pass

    dev.set_configuration()
    usb.util.claim_interface(dev, IFACE)
    dev.ctrl_transfer(0x40, 0x00, 0x0000, 1, None, 1000)
    dev.ctrl_transfer(0x40, 0x0B, 0x020B, 1, None, 1000)
    dev.ctrl_transfer(0x40, 0x09, 16, 1, None, 1000)
    flush(dev)

    r = xfer(dev, [0xAB], 2)
    print(f"adapter {vid:04x}:{pid:04x}   sync 0xAB -> {r.hex(' ')}"
          f"   {'OK' if bytes(r[:2]) == bytes([0xFA, 0xAB]) else 'FAIL'}")

    dev.write(EP_OUT, bytes([0x8A, 0x86, 0x1D, 0x00, 0x85,
                             0x80, 0x00, 0x0B]), 1000)
    flush(dev)

    # Shift 64 bits so a whole IDCODE is visible even if it is offset.
    tap_reset_to_shift_dr(dev)
    data = xfer(dev, [0x39, 0x07, 0x00] + [0x00] * 8, 8)
    print(f"\nTDI=0, 64 bits:  {data.hex(' ')}")
    b0 = bits_of(data)
    print(f"  first bit off wire is leftmost:\n  {b0}")

    # Repeat with TDI=1: bits that follow the register contents flip, which
    # makes the boundary between real data and shifted-in padding obvious.
    tap_reset_to_shift_dr(dev)
    data1 = xfer(dev, [0x39, 0x07, 0x00] + [0xFF] * 8, 8)
    print(f"\nTDI=1, 64 bits:  {data1.hex(' ')}")
    print(f"  {bits_of(data1)}")

    # Interpret the first 32 bits, and the same window shifted by one, since a
    # single-bit offset is the most likely alignment failure.
    v0 = int.from_bytes(data[:4], "little")
    shifted = int(b0[1:33][::-1], 2) if len(b0) >= 33 else 0
    print(f"\nas-is        IDCODE = 0x{v0:08x}   bit0={v0 & 1}")
    print(f"skip 1 bit   IDCODE = 0x{shifted:08x}   bit0={shifted & 1}")
    print("\nA valid JTAG IDCODE always has bit0 = 1 and a non-zero manufacturer.")

    usb.util.release_interface(dev, IFACE)

if __name__ == "__main__":
    main()
