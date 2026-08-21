#!/usr/bin/env python3
"""Raw libusb probe -- bypasses pyftdi's status-byte stripping.

pyftdi hides the 2-byte prefix, so "no packets at all" and "packets containing
only status bytes" both look like empty reads. Those are different bugs. This
shows the actual bytes on the wire.

    pip install pyusb
    python3 test/rawprobe.py        # sudo if you get an access error

Read the result as:
    all reads time out          -> device sends nothing: IN path dead
    repeated "01 60"            -> status cadence fine, OUT/MPSSE path dead
    "01 60 fa ab"               -> firmware is correct, pyftdi is the problem
"""
import sys
import usb.core
import usb.util

VID, PID = 0x0403, 0x6014
EP_IN, EP_OUT, IFACE = 0x81, 0x02, 0   # wIndex 1 = FTDI INTERFACE_A

def main():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("device not found")

    try:
        if dev.is_kernel_driver_active(IFACE):
            print("detaching ftdi_sio from interface 0")
            dev.detach_kernel_driver(IFACE)
    except (NotImplementedError, usb.core.USBError) as e:
        print(f"(kernel driver check skipped: {e})")

    dev.set_configuration()
    usb.util.claim_interface(dev, IFACE)

    # FTDI control sequence: reset, enter MPSSE, set latency
    dev.ctrl_transfer(0x40, 0x00, 0x0000, 1, None, 1000)   # SIO_RESET
    dev.ctrl_transfer(0x40, 0x0B, 0x020B, 1, None, 1000)   # BITMODE mpsse, mask 0x0b
    dev.ctrl_transfer(0x40, 0x09, 16,     1, None, 1000)   # latency 16 ms
    print("control sequence OK")

    print("\n-- draining before write (expect a few '01 60') --")
    for _ in range(4):
        try:
            r = dev.read(EP_IN, 64, 300)
            print(f"   drain len={len(r)}: {bytes(r).hex(' ') or '(empty)'}")
        except usb.core.USBError:
            print("   drain: timeout")

    n = dev.write(EP_OUT, bytes([0xAB]), 1000)
    print(f"\nwrote {n} byte(s): ab")

    print("\n-- reads after write (want '01 60 fa ab') --")
    for i in range(6):
        try:
            r = dev.read(EP_IN, 64, 500)
            print(f"   IN[{i}] len={len(r)}: {bytes(r).hex(' ') or '(empty)'}")
        except usb.core.USBError as e:
            print(f"   IN[{i}] timeout: {e}")

    usb.util.release_interface(dev, IFACE)

if __name__ == "__main__":
    main()
