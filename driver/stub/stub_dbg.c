#include "stub_driver.h"
#include "stub_dev.h"
#include "dbgcommon.h"
#include "usbip_stub_api.h"
#include "strutil.h"

#include <ntstrsafe.h>

const char *dbg_device(DEVICE_OBJECT *devobj)
{
	if (!devobj) {
		return "null";
	}

	if (!devobj->DriverObject) {
		return "driver null";
	}

	ANSI_STRING name;

	if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&name, &devobj->DriverObject->DriverName, TRUE))) {
		static char buf[32];
		RtlStringCchCopyA(buf, 32, name.Buffer);
		RtlFreeAnsiString(&name);
		return buf;
	}

	return "error";
}

const char *dbg_devices(DEVICE_OBJECT *devobj, BOOLEAN is_attached)
{
	static char	buf[1024];
	int	n = 0;
	int	i;

	for (i = 0; i < 16; i++) {
		if (devobj == NULL)
			break;
		n += libdrv_snprintf(buf + n, 1024 - n, "[%s]", dbg_device(devobj));
		if (is_attached)
			devobj = devobj->AttachedDevice;
		else
			devobj = devobj->NextDevice;
	}
	return buf;
}

const char *dbg_devstub(usbip_stub_dev_t *devstub)
{
	if (!devstub) {
		return "<null>";
	}

	static char buf[512];
	RtlStringCchPrintfA(buf, 512, "id:%d,hw:%s", devstub->id, devstub->id_hw);
	return buf;
}

const char *dbg_stub_ioctl_code(int ioctl_code)
{
	static_assert(sizeof(ioctl_code) == sizeof(IOCTL_USBIP_STUB_EXPORT), "assert");

	switch (ioctl_code) {
	case IOCTL_USBIP_STUB_GET_DEVINFO: return "IOCTL_USBIP_STUB_GET_DEVINFO";
	case IOCTL_USBIP_STUB_EXPORT: return "IOCTL_USBIP_STUB_EXPORT";
	}

	return "IOCTL_USBIP_STUB_?";
}
