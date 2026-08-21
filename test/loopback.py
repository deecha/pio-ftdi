#!/usr/bin/env python3
"""Physical loopback test.  ***Jumper TDI (pin 39) to TDO (pin 40) first.***

This is the test that separates "the PIO is broken" from "the TAP sequencing is
broken", and it needs no scope and no FPGA.

Everything so far read TCK/TDI/TMS from software-tracked variables, never from
hardware -- so we have no evidence any pin actually toggles. With TDI wired to
TDO, whatever we shift out must come straight back in.

    PASS  -> PIO drives TDI on the right pin, reads TDO on the right pin, and
             bit alignment is correct end to end. The bug is in TMS/TAP
             sequencing, not the shifter.
    FAIL  -> pins or shifting are wrong. Nothing above that layer matters.

Remove the jumper afterwards.

    python3 test/loopback.py
"""
import sys
import usb.core
import usb.util

VID, PID, EP_IN, EP_OUT, IFACE = 0x0403, 0x6014, 0x81, 0x02, 0

def flush(dev, tries=6):
    for _ in range(tries):
        try:
            dev.read(EP_IN, 64, 20)
        except usb.core.USBError:
            return

def xfer(dev, cmd, want):
    dev.write(EP_OUT, bytes(cmd) + b"\x87", 1000)
    got = b""
    for _ in range(12):
        try:
            r = bytes(dev.read(EP_IN, 64, 100))
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
    dev.ctrl_transfer(0x40, 0x09, 16, 1, None, 1000)
    flush(dev)

    r = xfer(dev, [0xAB], 2)
    ok = r[:2] == b"\xfa\xab"
    print(f"sync 0xAB -> {r.hex(' ')}  {'OK' if ok else 'FAIL'}")
    if not ok:
        print("MPSSE not responding; fix that before trusting anything below.")
        return

    # 1 MHz TCK, software loopback OFF so we test the real pins
    dev.write(EP_OUT, bytes([0x8A, 0x86, 0x1D, 0x00, 0x85, 0x80, 0x00, 0x0B]), 1000)
    flush(dev)

    patterns = [0xA5, 0x5A, 0xFF, 0x00, 0x01, 0x80, 0x3C]
    print("\n  sent  recv")
    passed = 0
    for p in patterns:
        # 0x39 = clock bytes out on -ve edge, in on +ve edge, LSB first
        got = xfer(dev, [0x39, 0x00, 0x00, p], 1)
        if not got:
            print(f"  0x{p:02x}   (nothing)")
            continue
        g = got[0]
        mark = "ok" if g == p else "MISMATCH"
        print(f"  0x{p:02x}  0x{g:02x}   {mark}")
        if g == p:
            passed += 1

    print(f"\n{passed}/{len(patterns)} patterns matched")
    if passed == len(patterns):
        print("PASS - PIO drives TDI, reads TDO, alignment correct.")
        print("       The bug is in TMS / TAP sequencing.")
    elif passed == 0:
        print("FAIL - no data returned at all.")
        print("       Either the jumper is not fitted, or the PIO is not")
        print("       driving/reading the pins you think it is.")
    else:
        print("PARTIAL - some bits survive. Compare sent vs recv bit patterns:")
        print("       a consistent shift means an off-by-one in alignment.")

    usb.util.release_interface(dev, IFACE)

if __name__ == "__main__":
    main()
