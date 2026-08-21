#!/usr/bin/env python3
"""Read the OUT-path debug counters (vendor request 0xFE).

Writes 0xAB first, then dumps the counters, so you can see exactly how far
down the chain the byte got.

    pip install pyusb
    python3 test/dbg.py
"""
import struct
import sys
import usb.core
import usb.util

VID, PID = 0x0403, 0x6014
EP_IN, EP_OUT, IFACE = 0x81, 0x02, 0

FIELDS = [
    ("open_a",    "ftdi_drv_open() ran for interface 0"),
    ("arm_call",  "arm_out() entered"),
    ("arm_ok",    "usbd_edpt_xfer(EPA_OUT) accepted"),
    ("cb_out",    "xfer_cb fired for EPA_OUT"),
    ("cb_bytes",  "bytes delivered by those callbacks"),
    ("feed_in",   "bytes handed to mpsse_feed"),
    ("feed_used", "bytes mpsse_feed consumed"),
    ("resp_now",  "response bytes waiting in ring"),
]

def read_counters(dev):
    raw = dev.ctrl_transfer(0xC0, 0xFE, 0, 1, 16, 1000)
    return dict(zip([f[0] for f in FIELDS], struct.unpack("<8H", bytes(raw))))

def dump(tag, c):
    print(f"\n--- {tag} ---")
    for name, desc in FIELDS:
        print(f"  {name:<10} {c[name]:>5}   {desc}")

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

    dev.ctrl_transfer(0x40, 0x00, 0x0000, 1, None, 1000)   # SIO_RESET
    dev.ctrl_transfer(0x40, 0x0B, 0x020B, 1, None, 1000)   # BITMODE mpsse
    dev.ctrl_transfer(0x40, 0x09, 16,     1, None, 1000)   # latency 16 ms

    dump("after control setup, before any write", read_counters(dev))

    n = dev.write(EP_OUT, bytes([0xAB]), 1000)
    print(f"\nwrote {n} byte(s): ab")

    try:
        r = dev.read(EP_IN, 64, 500)
        print(f"read back: {bytes(r).hex(' ')}")
    except usb.core.USBError:
        print("read back: timeout")

    dump("after writing 0xAB", read_counters(dev))

    print("\nsecond write (does the endpoint get re-armed?)")
    try:
        n = dev.write(EP_OUT, bytes([0xAB]), 1000)
        print(f"  wrote {n} byte(s)")
    except usb.core.USBError as e:
        print(f"  FAILED: {e}   <- endpoint was never re-armed")

    dump("after second write", read_counters(dev))
    usb.util.release_interface(dev, IFACE)

if __name__ == "__main__":
    main()
