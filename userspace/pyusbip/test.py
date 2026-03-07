# Copyright (c) 2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>

import usbip as u

dev = u.open()

devices, ok = u.get_persistent(dev) # usbip list -s
if ok:
    print("persistent devices")
    for d in devices:
        print(d)
else:
    print(f"Failed to get persistent devices")

devices, ok = u.get_imported_devices(dev)
if ok:
    print("imported devices")
    for d in devices:
        print(f"{d.location}, port {d.port}, devid {d.devid}, speed {d.speed}, {d.vendor}, {d.product}")
else:
    print(f"Failed to get imported devices")

d = u.device_location()
d.hostname = "pc"
d.service = u.get_tcp_port()
d.busid = "3-3"

port = u.attach(dev, d)
if port > 0:
    print(f"attached to port #{port}")
else:
    print(f"attach error")
