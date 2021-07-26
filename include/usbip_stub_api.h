#pragma once

#include <guiddef.h>
#ifdef _NTDDK_
#include <ntddk.h>
#else
#include <winioctl.h>
#endif

// {FB265267-C609-41E6-8ECA-A20D92A833E6}
DEFINE_GUID(GUID_DEVINTERFACE_STUB_USBIP,
	0xfb265267, 0xc609, 0x41e6, 0x8e, 0xca, 0xa2, 0xd, 0x92, 0xa8, 0x33, 0xe6);

#define USBIP_STUB_IOCTL(_index_) \
    CTL_CODE(FILE_DEVICE_UNKNOWN, _index_, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_USBIP_STUB_GET_DEVINFO	USBIP_STUB_IOCTL(0x0)
#define IOCTL_USBIP_STUB_EXPORT		USBIP_STUB_IOCTL(0x1)

#include <PSHPACK1.H>

typedef struct _ioctl_usbip_stub_devinfo
{
	unsigned short	vendor;
	unsigned short	product;
	unsigned char	speed;
	unsigned char	class;
	unsigned char	subclass;
	unsigned char	protocol;
} ioctl_usbip_stub_devinfo_t;

#include <POPPACK.H>
