#include "stub_driver.h"
#include "stub_dev.h"
#include "dbgcommon.h"
#include "usbip_stub_api.h"

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
	NTSTATUS st = RtlUnicodeStringToAnsiString(&name, &devobj->DriverObject->DriverName, TRUE);
	if (st != STATUS_SUCCESS) {
		return "dbg_device error";
	}
	
	static char buf[32];
	RtlStringCbCopyA(buf, min(sizeof(buf), name.Length + 1), name.Buffer);

	RtlFreeAnsiString(&name);
	return buf;
}

const char *dbg_devices(DEVICE_OBJECT *devobj, BOOLEAN is_attached)
{
	static char buf[1024];

	*buf = '\0';

	NTSTRSAFE_PSTR end = buf;
	size_t remaining = sizeof(buf);

	for (int i = 0; i < 16 && devobj; ++i) {

		NTSTATUS st = RtlStringCbPrintfExA(end, remaining, &end, &remaining, 0, "[%s]", dbg_device(devobj));

		if (st == STATUS_SUCCESS) {
			devobj = is_attached ? devobj->AttachedDevice : devobj->NextDevice;
		} else {
			break;
		}
	}

	return buf;
}

const char *dbg_devstub(usbip_stub_dev_t *devstub)
{
	if (!devstub) {
		return "<null>";
	}

	static char buf[512];

	NTSTATUS st = RtlStringCbPrintfA(buf, sizeof(buf), "id:%d,hw:%s", devstub->id, devstub->id_hw);
	return st == STATUS_SUCCESS ? buf : "dbg_devstub error";
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
