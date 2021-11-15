#include "dbgcode.h"

#include <ntstrsafe.h>
#include "strutil.h"

enum { NAMECODE_BUF_MAX = 256 };

static const namecode_t namecodes_usbd_status[] = {
	K_V(USBD_STATUS_SUCCESS)
	K_V(USBD_STATUS_PENDING)
	K_V(USBD_STATUS_STALL_PID)
	{0,0}
};

static const namecode_t namecodes_wmi_minor[] = {
	K_V(IRP_MN_CHANGE_SINGLE_INSTANCE)
	K_V(IRP_MN_CHANGE_SINGLE_ITEM)
	K_V(IRP_MN_DISABLE_COLLECTION)
	K_V(IRP_MN_DISABLE_EVENTS)
	K_V(IRP_MN_ENABLE_COLLECTION)
	K_V(IRP_MN_ENABLE_EVENTS)
	K_V(IRP_MN_EXECUTE_METHOD)
	K_V(IRP_MN_QUERY_ALL_DATA)
	K_V(IRP_MN_QUERY_SINGLE_INSTANCE)
	K_V(IRP_MN_REGINFO)
	{0,0}
};

static const namecode_t namecodes_power_minor[] = {
	K_V(IRP_MN_SET_POWER)
	K_V(IRP_MN_QUERY_POWER)
	K_V(IRP_MN_POWER_SEQUENCE)
	K_V(IRP_MN_WAIT_WAKE)
	{0,0}
};

static const namecode_t namecodes_usb_descriptor_type[] = {
	K_V(USB_DEVICE_DESCRIPTOR_TYPE)
	K_V(USB_CONFIGURATION_DESCRIPTOR_TYPE)
	K_V(USB_STRING_DESCRIPTOR_TYPE)
	K_V(USB_INTERFACE_DESCRIPTOR_TYPE)
	K_V(USB_ENDPOINT_DESCRIPTOR_TYPE)
	{0,0}
};

const char *
dbg_namecode_buf(const namecode_t *namecodes, const char *codetype, unsigned int code, char *buf, int buf_max)
{
	ULONG	nwritten = 0;
	ULONG	n_codes = 0;
	int i;

	/* assume: duplicated codes may exist */
	for (i = 0; namecodes[i].name; i++) {
		if (code == namecodes[i].code) {
			if (nwritten > 0)
				nwritten += libdrv_snprintf(buf + nwritten, buf_max - nwritten, ",%s", namecodes[i].name);
			else
				nwritten = libdrv_snprintf(buf, buf_max, "%s", namecodes[i].name);
			n_codes++;
		}
	}
	if (n_codes == 0)
		libdrv_snprintf(buf, buf_max, "Unknown %s code: %x", codetype, code);
	return buf;
}

const char *
dbg_namecode(const namecode_t *namecodes, const char *codetype, unsigned int code)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes, codetype, code, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_usbd_status(USBD_STATUS status)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes_usbd_status, "usbd status", status, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_wmi_minor(UCHAR minor)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes_wmi_minor, "wmi minor function", minor, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_power_minor(UCHAR minor)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes_power_minor, "power minor function", minor, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_usb_descriptor_type(UCHAR dsc_type)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes_usb_descriptor_type, "descriptor type", dsc_type, buf, NAMECODE_BUF_MAX);
}

