#include "stub_driver.h"
#include "stub_dbg.h"
#include "stub_dev.h"
#include "dbgcommon.h"
#include "usbip_stub_api.h"

#include <ntstrsafe.h>

const char *dbg_device(char *buf, unsigned int len, const DEVICE_OBJECT *devobj)
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
		return "dbg_device translate error";
	}
	
	st = RtlStringCbCopyA(buf, min(name.Length + 1U, len), name.Buffer);
	RtlFreeAnsiString(&name);

	return st == STATUS_SUCCESS ? buf : "dbg_device copy error";
}

const char *dbg_devices(char *buf, unsigned int len, const DEVICE_OBJECT *devobj, bool is_attached)
{
	*buf = '\0';

	NTSTRSAFE_PSTR end = buf;
	size_t remaining = len;

	for (int i = 0; i < DBG_DEVICES_BUFSZ/DBG_DEVICE_BUFSZ && devobj; ++i) {

		char dev_buf[DBG_DEVICE_BUFSZ];
		
		NTSTATUS st = RtlStringCbPrintfExA(end, remaining, &end, &remaining, 0, "[%s]", 
			dbg_device(dev_buf, sizeof(dev_buf), devobj));

		if (st != STATUS_SUCCESS) {
			break;
		}

		devobj = is_attached ? devobj->AttachedDevice : devobj->NextDevice;
	}

	return buf;
}

const char *dbg_devstub(char *buf, unsigned int len, const usbip_stub_dev_t *devstub)
{
	if (!devstub) {
		return "<null>";
	}

	NTSTATUS st = RtlStringCbPrintfA(buf, len, "id:%d,hw:%s", devstub->id, devstub->id_hw);
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
