/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include <usbip/proto_op.h>

extern "C" {
unsigned short __cdecl _byteswap_ushort(unsigned short);
unsigned long __cdecl _byteswap_ulong(unsigned long);
}
#pragma intrinsic(_byteswap_ushort, _byteswap_ulong)

namespace
{

inline void bswap(UINT16 &val)
{
        static_assert(sizeof(val) == sizeof(unsigned short));
        val = _byteswap_ushort(val);
}

inline void bswap(UINT32 &val)
{
        static_assert(sizeof(val) == sizeof(unsigned long));
        val = _byteswap_ulong(val);
}

} // namespace


void usbip::byteswap(usbip_usb_device &d)
{
        bswap(d.busnum);
        bswap(d.devnum);
        bswap(d.speed);

        bswap(d.idVendor);
        bswap(d.idProduct);
        bswap(d.bcdDevice);
}

void usbip::byteswap(op_common &c)
{
        bswap(c.version);
        bswap(c.code);
        bswap(c.status);
}

void usbip::byteswap(op_devlist_reply &r)
{
        bswap(r.ndev);
}
