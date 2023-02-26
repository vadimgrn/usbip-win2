#include <usbip\proto_op.h>
#include <intrin.h>

void usbip_net_pack_uint32_t(int, UINT32 *num)
{
        static_assert(sizeof(*num) == sizeof(unsigned long));
        *num = _byteswap_ulong(*num);
}

void usbip_net_pack_uint16_t(int, UINT16 *num)
{
        static_assert(sizeof(*num) == sizeof(unsigned short));
        *num = _byteswap_ushort(*num);
}

void usbip_net_pack_usb_device(int pack, usbip_usb_device *udev)
{
        usbip_net_pack_uint32_t(pack, &udev->busnum);
        usbip_net_pack_uint32_t(pack, &udev->devnum);
        usbip_net_pack_uint32_t(pack, &udev->speed);

        usbip_net_pack_uint16_t(pack, &udev->idVendor);
        usbip_net_pack_uint16_t(pack, &udev->idProduct);
        usbip_net_pack_uint16_t(pack, &udev->bcdDevice);
}

void usbip_net_pack_usb_interface(int, usbip_usb_interface*)
{
        /* UINT8 members need nothing */
}
