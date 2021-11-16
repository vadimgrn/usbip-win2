#include "stub_driver.h"
#include "dbgcommon.h"
#include "stub_cspkt.h"
#include "strutil.h"

#include <usbspec.h>

static namecode_t	namecodes_cspkt_reqtype[] = {
	K_V(BMREQUEST_STANDARD)
	K_V(BMREQUEST_CLASS)
	K_V(BMREQUEST_VENDOR)
	{0,0}
};

static namecode_t	namecodes_cspkt_recipient[] = {
	K_V(BMREQUEST_TO_DEVICE)
	K_V(BMREQUEST_TO_INTERFACE)
	K_V(BMREQUEST_TO_ENDPOINT)
	K_V(BMREQUEST_TO_OTHER)
	{0,0}
};

static namecode_t	namecodes_cspkt_request[] = {
	K_V(USB_REQUEST_GET_STATUS)
	K_V(USB_REQUEST_CLEAR_FEATURE)
	K_V(USB_REQUEST_SET_FEATURE)
	K_V(USB_REQUEST_SET_ADDRESS)
	K_V(USB_REQUEST_GET_DESCRIPTOR)
	K_V(USB_REQUEST_SET_DESCRIPTOR)
	K_V(USB_REQUEST_GET_CONFIGURATION)
	K_V(USB_REQUEST_SET_CONFIGURATION)
	K_V(USB_REQUEST_GET_INTERFACE)
	K_V(USB_REQUEST_SET_INTERFACE)
	K_V(USB_REQUEST_SYNC_FRAME)
	{0,0}
};

static namecode_t	namecodes_cspkt_desctype[] = {
	K_V(USB_DEVICE_DESCRIPTOR_TYPE)
	K_V(USB_CONFIGURATION_DESCRIPTOR_TYPE)
	K_V(USB_STRING_DESCRIPTOR_TYPE)
	K_V(USB_INTERFACE_DESCRIPTOR_TYPE)
	K_V(USB_ENDPOINT_DESCRIPTOR_TYPE)
	{0,0}
};

const char *
dbg_cspkt_reqtype(UCHAR reqtype)
{
	return dbg_namecode(namecodes_cspkt_reqtype, "reqtype", reqtype);
}

const char *
dbg_cspkt_recipient(UCHAR recip)
{
	return dbg_namecode(namecodes_cspkt_recipient, "recipient", recip);
}

const char *
dbg_cspkt_request(UCHAR req)
{
	return dbg_namecode(namecodes_cspkt_request, "request", req);
}

const char *
dbg_cspkt_desctype(UCHAR desctype)
{
	return dbg_namecode(namecodes_cspkt_desctype, "descriptor type", desctype);
}

const char *
dbg_ctlsetup_packet(const USB_DEFAULT_PIPE_SETUP_PACKET *csp)
{
	static char	buf[1024];
	int	n;

	n = libdrv_snprintf(buf, 1024, "%s", CSPKT_IS_IN(csp) ? "in": "out");
	n += libdrv_snprintf(buf + n, 1024 - n, ",%s", dbg_cspkt_reqtype(CSPKT_REQUEST_TYPE(csp)));
	n += libdrv_snprintf(buf + n, 1024 - n, ",%s", dbg_cspkt_recipient(CSPKT_RECIPIENT(csp)));
	n += libdrv_snprintf(buf + n, 1024 - n, ",%s", dbg_cspkt_request(CSPKT_REQUEST(csp)));
	n += libdrv_snprintf(buf + n, 1024 - n, ",wIndex:%hu", csp->wIndex);
	n += libdrv_snprintf(buf + n, 1024 - n, ",wLength:%hu", csp->wLength);
	n += libdrv_snprintf(buf + n, 1024 - n, ",wValue:%hu", csp->wValue);

	return buf;
}
