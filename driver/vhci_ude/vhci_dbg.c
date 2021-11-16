#include "vhci_dbg.h"
#include "vhci_driver.h"

#include "dbgcode.h"

#include <usbdi.h>
#include <usbspec.h>

#include "strutil.h"
#include "usbip_vhci_api.h"

#include "vhci_urbr.h"

/*
 * WPP call requires both a debug message buffer and the length at the same time.
 * Thus, WPP macros reference global variables, which are manipluated via dbg_xxxx().
 */
char	buf_dbg_vhci_ioctl_code[128];
char	buf_dbg_setup_packet[128];
char	buf_dbg_urbr[128];

int	len_dbg_vhci_ioctl_code;
int	len_dbg_setup_packet;
int	len_dbg_urbr;

/*
 * NOTE: WPP tracing requires debug message routines even for a debug configuration.
 * So, debug routines in this file will be built against a release configuration.
 */
enum { NAMECODE_BUF_MAX = 256 };

#define K_V(a) {#a, (unsigned int)a},

typedef struct {
	const char *name;
	unsigned int code;
} namecode_my_t;

static const char *
dbg_namecode_buf_len(namecode_my_t *namecodes, const char *codetype, unsigned int code, char *buf, int buf_max, int *plen)
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
		nwritten += libdrv_snprintf(buf, buf_max, "Unknown %s code: %x", codetype, code);
	*plen = nwritten + 1;
	return buf;
}

static namecode_my_t	namecodes_vhci_ioctl[] = {
	K_V(IOCTL_USBIP_VHCI_PLUGIN_HARDWARE)
	K_V(IOCTL_USBIP_VHCI_UNPLUG_HARDWARE)
	K_V(IOCTL_USBIP_VHCI_GET_PORTS_STATUS)
	K_V(IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES)
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
	{0,0}
};

const char *
dbg_vhci_ioctl_code(unsigned int ioctl_code)
{
	return dbg_namecode_buf_len(namecodes_vhci_ioctl, "ioctl", ioctl_code, buf_dbg_vhci_ioctl_code, 128, &len_dbg_vhci_ioctl_code);
}

const char *
dbg_usb_setup_packet(PCUCHAR packet)
{
	PUSB_DEFAULT_PIPE_SETUP_PACKET	pkt = (PUSB_DEFAULT_PIPE_SETUP_PACKET)packet;

	len_dbg_setup_packet = libdrv_snprintf(buf_dbg_setup_packet, 128, "rqtype:%02x,req:%02x,wIndex:%hu,wLength:%hu,wValue:%hu",
		pkt->bmRequestType, pkt->bRequest, pkt->wIndex, pkt->wLength, pkt->wValue);
	len_dbg_setup_packet++;
	return buf_dbg_setup_packet;
}

const char *
dbg_urbr(purb_req_t urbr)
{
	if (urbr == NULL) {
		len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, 128, "[null]");
	}
	else {
		switch (urbr->type) {
		case URBR_TYPE_URB:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, 128, "[urb,seq:%u,!%urb_function%]", urbr->seq_num, urbr->u.urb.urb->UrbHeader.Function);
			break;
		case URBR_TYPE_UNLINK:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, 128, "[ulk,seq:%u,%u]", urbr->seq_num, urbr->u.seq_num_unlink);
			break;
		case URBR_TYPE_SELECT_CONF:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, 128, "[slc,seq:%u,%hhu]", urbr->seq_num, urbr->u.conf_value);
			break;
		case URBR_TYPE_SELECT_INTF:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, 128, "[sli,seq:%u,%hhu,%hhu]", urbr->seq_num, urbr->u.intf.intf_num, urbr->u.intf.alt_setting);
			break;
		case URBR_TYPE_RESET_PIPE:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, 128, "[rst,seq:%u,%hhu]", urbr->seq_num, urbr->ep->addr);
			break;
		default:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, 128, "[unk:seq:%u]", urbr->seq_num);
			break;
		}
	}
	len_dbg_urbr++;
	return buf_dbg_urbr;
}
