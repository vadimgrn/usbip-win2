#include "dbgcode.h"
#include "strutil.h"
#include "usbip_vhci_api.h"

#include <ntstrsafe.h>
#include <usbdi.h>
#include <usbuser.h>

enum { NAMECODE_BUF_MAX = 256 };

char buf_dbg_vhci_ioctl_code[NAMECODE_BUF_MAX];
unsigned int len_dbg_vhci_ioctl_code;

static const namecode_t namecodes_usbd_status[] = 
{
	K_V(USBD_STATUS_SUCCESS)
	K_V(USBD_STATUS_PENDING)
	K_V(USBD_STATUS_STALL_PID)
	{0, 0}
};

static const namecode_t namecodes_vhci_ioctl[] = 
{
	K_V(IOCTL_USBIP_VHCI_PLUGIN_HARDWARE)
	K_V(IOCTL_USBIP_VHCI_UNPLUG_HARDWARE)
	K_V(IOCTL_USBIP_VHCI_GET_PORTS_STATUS)
	K_V(IOCTL_INTERNAL_USB_CYCLE_PORT)
	K_V(IOCTL_INTERNAL_USB_ENABLE_PORT)
	K_V(IOCTL_INTERNAL_USB_GET_BUS_INFO)
	K_V(IOCTL_INTERNAL_USB_GET_BUSGUID_INFO)
	K_V(IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME)
	K_V(IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE)
	K_V(IOCTL_INTERNAL_USB_GET_HUB_COUNT)
	K_V(IOCTL_INTERNAL_USB_GET_HUB_NAME)
	K_V(IOCTL_INTERNAL_USB_GET_PARENT_HUB_INFO)
	K_V(IOCTL_INTERNAL_USB_GET_PORT_STATUS)
	K_V(IOCTL_INTERNAL_USB_RESET_PORT)
	K_V(IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO)
	K_V(IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION)
	K_V(IOCTL_INTERNAL_USB_SUBMIT_URB)
	K_V(IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS)
	K_V(IOCTL_USB_DIAG_IGNORE_HUBS_ON)
	K_V(IOCTL_USB_DIAG_IGNORE_HUBS_OFF)
	K_V(IOCTL_USB_DIAGNOSTIC_MODE_OFF)
	K_V(IOCTL_USB_DIAGNOSTIC_MODE_ON)
	K_V(IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION)
	K_V(IOCTL_USB_GET_HUB_CAPABILITIES)
	K_V(IOCTL_USB_GET_ROOT_HUB_NAME)
	K_V(IOCTL_GET_HCD_DRIVERKEY_NAME)
	K_V(IOCTL_USB_GET_NODE_INFORMATION)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_INFORMATION)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_NAME)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME)
	K_V(IOCTL_USB_HCD_DISABLE_PORT)
	K_V(IOCTL_USB_HCD_ENABLE_PORT)
	K_V(IOCTL_USB_HCD_GET_STATS_1)
	K_V(IOCTL_USB_HCD_GET_STATS_2)
	K_V(IOCTL_USB_USER_REQUEST)
	K_V(IOCTL_USB_GET_HUB_CAPABILITIES)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES)
	K_V(IOCTL_USB_HUB_CYCLE_PORT)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX)
	K_V(IOCTL_USB_RESET_HUB)
	K_V(IOCTL_USB_GET_HUB_CAPABILITIES_EX)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES)
	K_V(IOCTL_USB_GET_HUB_INFORMATION_EX)
	K_V(IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES)
	K_V(IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2)
	{0, 0}
};

const char *dbg_namecode_buf(
	const namecode_t *namecodes, const char *codetype, unsigned int code, 
	char *buf, unsigned int buf_max, unsigned int *written)
{
	unsigned int cnt = 0;

	for ( ; namecodes->name; ++namecodes) {
		if (code == namecodes->code) {
			cnt += libdrv_snprintf(buf, buf_max, "%s", namecodes->name);
			break;
		}
	}

	if (!cnt) {
		cnt += libdrv_snprintf(buf, buf_max, "Unknown %s code: %x", codetype, code);
	}

	if (written && cnt) {
		*written = cnt + 1; // '\0'
	}
	
	return buf;
}

const char *dbg_namecode(const namecode_t *namecodes, const char *codetype, unsigned int code)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes, codetype, code, buf, sizeof(buf), NULL);
}

const char *dbg_usbd_status(USBD_STATUS status)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes_usbd_status, "usbd status", status, buf, sizeof(buf), NULL);
}


/*
 * See: DEFINE_CPLX_TYPE(IOCTL,...) in custom_wpp.ini 
 */
const char *dbg_vhci_ioctl_code(unsigned int ioctl_code)
{
	return dbg_namecode_buf(namecodes_vhci_ioctl, "ioctl", ioctl_code,
		buf_dbg_vhci_ioctl_code, sizeof(buf_dbg_vhci_ioctl_code),
		&len_dbg_vhci_ioctl_code);
}
