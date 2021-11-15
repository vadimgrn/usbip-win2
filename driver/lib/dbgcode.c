#include "dbgcode.h"

#include <ntstrsafe.h>
#include "strutil.h"

static namecode_t namecodes_usbd_status[] = {
	K_V(USBD_STATUS_SUCCESS)
	K_V(USBD_STATUS_PENDING)
	K_V(USBD_STATUS_STALL_PID)
	{0,0}
};

static namecode_t	namecodes_dispatch_major[] = {
	K_V(IRP_MJ_READ)
	K_V(IRP_MJ_WRITE)
	K_V(IRP_MJ_PNP)
	K_V(IRP_MJ_POWER)
	K_V(IRP_MJ_DEVICE_CONTROL)
	K_V(IRP_MJ_CREATE)
	K_V(IRP_MJ_CLOSE)
	K_V(IRP_MJ_CLEANUP)
	{0, 0}
};

static namecode_t	namecodes_pnp_minor[] = {
	K_V(IRP_MN_START_DEVICE)
	K_V(IRP_MN_QUERY_REMOVE_DEVICE)
	K_V(IRP_MN_REMOVE_DEVICE)
	K_V(IRP_MN_CANCEL_REMOVE_DEVICE)
	K_V(IRP_MN_STOP_DEVICE)
	K_V(IRP_MN_QUERY_STOP_DEVICE)
	K_V(IRP_MN_CANCEL_STOP_DEVICE)
	K_V(IRP_MN_QUERY_DEVICE_RELATIONS)
	K_V(IRP_MN_QUERY_INTERFACE)
	K_V(IRP_MN_QUERY_CAPABILITIES)
	K_V(IRP_MN_QUERY_RESOURCES)
	K_V(IRP_MN_QUERY_RESOURCE_REQUIREMENTS)
	K_V(IRP_MN_QUERY_DEVICE_TEXT)
	K_V(IRP_MN_FILTER_RESOURCE_REQUIREMENTS)
	K_V(IRP_MN_READ_CONFIG)
	K_V(IRP_MN_WRITE_CONFIG)
	K_V(IRP_MN_EJECT)
	K_V(IRP_MN_SET_LOCK)
	K_V(IRP_MN_QUERY_ID)
	K_V(IRP_MN_QUERY_PNP_DEVICE_STATE)
	K_V(IRP_MN_QUERY_BUS_INFORMATION)
	K_V(IRP_MN_DEVICE_USAGE_NOTIFICATION)
	K_V(IRP_MN_SURPRISE_REMOVAL)
	K_V(IRP_MN_QUERY_LEGACY_BUS_INFORMATION)
	K_V(IRP_MN_DEVICE_ENUMERATED)
	{0,0}
};

static namecode_t	namecodes_bus_query_id[] = {
	K_V(BusQueryDeviceID)
	K_V(BusQueryHardwareIDs)
	K_V(BusQueryCompatibleIDs)
	K_V(BusQueryInstanceID)
	K_V(BusQueryDeviceSerialNumber)
	K_V(BusQueryContainerID)
	{0,0}
};

static namecode_t	namecodes_dev_relation[] = {
	K_V(BusRelations)
	K_V(PowerRelations)
	K_V(EjectionRelations)
	K_V(RemovalRelations)
	K_V(TargetDeviceRelation)
	{0,0}
};

static namecode_t	namecodes_wmi_minor[] = {
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

static namecode_t	namecodes_power_minor[] = {
	K_V(IRP_MN_SET_POWER)
	K_V(IRP_MN_QUERY_POWER)
	K_V(IRP_MN_POWER_SEQUENCE)
	K_V(IRP_MN_WAIT_WAKE)
	{0,0}
};

static namecode_t	namecodes_system_power[] = {
	K_V(PowerSystemUnspecified)
	K_V(PowerSystemWorking)
	K_V(PowerSystemSleeping2)
	K_V(PowerSystemSleeping3)
	K_V(PowerSystemHibernate)
	K_V(PowerSystemShutdown)
	K_V(PowerSystemMaximum)
	{0,0}
};

static namecode_t	namecodes_device_power[] = {
	K_V(PowerDeviceUnspecified)
	K_V(PowerDeviceD0)
	K_V(PowerDeviceD1)
	K_V(PowerDeviceD2)
	K_V(PowerDeviceD3)
	K_V(PowerDeviceMaximum)
	K_V(IRP_MN_EXECUTE_METHOD)
	K_V(IRP_MN_QUERY_ALL_DATA)
	K_V(IRP_MN_QUERY_SINGLE_INSTANCE)
	K_V(IRP_MN_REGINFO)
	{0,0}
};

static namecode_t	namecodes_usb_descriptor_type[] = {
	K_V(USB_DEVICE_DESCRIPTOR_TYPE)
	K_V(USB_CONFIGURATION_DESCRIPTOR_TYPE)
	K_V(USB_STRING_DESCRIPTOR_TYPE)
	K_V(USB_INTERFACE_DESCRIPTOR_TYPE)
	K_V(USB_ENDPOINT_DESCRIPTOR_TYPE)
	{0,0}
};

#define NAMECODE_BUF_MAX	256

const char *
dbg_namecode_buf(namecode_t *namecodes, const char *codetype, unsigned int code, char *buf, int buf_max)
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
dbg_namecode(namecode_t *namecodes, const char *codetype, unsigned int code)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes, codetype, code, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_usbd_status(USBD_STATUS status)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_usbd_status, "usbd status", status, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_dispatch_major(UCHAR major)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_dispatch_major, "dispatch major", (unsigned int)major, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_pnp_minor(UCHAR minor)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_pnp_minor, "pnp minor function", minor, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_bus_query_id_type(BUS_QUERY_ID_TYPE type)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_bus_query_id, "bus query id", type, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_dev_relation(DEVICE_RELATION_TYPE type)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_dev_relation, "device relation", type, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_wmi_minor(UCHAR minor)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_wmi_minor, "wmi minor function", minor, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_power_minor(UCHAR minor)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_power_minor, "power minor function", minor, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_system_power(SYSTEM_POWER_STATE state)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_system_power, "system power", (int)state, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_device_power(DEVICE_POWER_STATE state)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_device_power, "device power", (int)state, buf, NAMECODE_BUF_MAX);
}

const char *
dbg_usb_descriptor_type(UCHAR dsc_type)
{
	static char	buf[NAMECODE_BUF_MAX];

	return dbg_namecode_buf(namecodes_usb_descriptor_type, "descriptor type", dsc_type, buf, NAMECODE_BUF_MAX);
}

