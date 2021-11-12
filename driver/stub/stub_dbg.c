#include "stub_driver.h"
#include "stub_dev.h"
#include "dbgcommon.h"

#include <ntstrsafe.h>
#include "dbgcode.h"
#include "usbip_stub_api.h"

#include "strutil.h"

const char *
dbg_device(PDEVICE_OBJECT devobj)
{
	static char	buf[32];
	ANSI_STRING	name;

	if (devobj == NULL)
		return "null";
	if (devobj->DriverObject)
		return "driver null";
	if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&name, &devobj->DriverObject->DriverName, TRUE))) {
		RtlStringCchCopyA(buf, 32, name.Buffer);
		RtlFreeAnsiString(&name);
		return buf;
	}
	else {
		return "error";
	}
}

const char *
dbg_devices(PDEVICE_OBJECT devobj, BOOLEAN is_attached)
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

const char *
dbg_devstub(usbip_stub_dev_t *devstub)
{
	static char	buf[512];

	if (devstub == NULL)
		return "<null>";
	RtlStringCchPrintfA(buf, 512, "id:%d,hw:%s", devstub->id, devstub->id_hw);
	return buf;
}

static namecode_t	namecodes_stub_ioctl[] = {
	K_V(IOCTL_USBIP_STUB_GET_DEVINFO)
	K_V(IOCTL_USBIP_STUB_EXPORT)
	{0,0}
};

const char *
dbg_stub_ioctl_code(ULONG ioctl_code)
{
	return dbg_namecode(namecodes_stub_ioctl, "ioctl", ioctl_code);
}
