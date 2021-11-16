#include "dbgcommon.h"
#include "strutil.h"
#include "usbip_proto.h"
#include "usbip_vhci_api.h"

#include <usbdi.h>
#include <usbuser.h>
#include <ntstrsafe.h>

enum { NAMECODE_BUF_MAX = 256 };

char buf_dbg_vhci_ioctl_code[NAMECODE_BUF_MAX];
unsigned int len_dbg_vhci_ioctl_code;

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
	const namecode_t* namecodes, const char* codetype, unsigned int code,
	char* buf, unsigned int buf_max, unsigned int* written)
{
	unsigned int cnt = 0;

	for (; namecodes->name; ++namecodes) {
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

const char *dbg_namecode(const namecode_t* namecodes, const char* codetype, unsigned int code)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes, codetype, code, buf, sizeof(buf), NULL);
}

const char *dbg_usbd_status(USBD_STATUS status)
{
        switch (status) {
        case USBD_STATUS_SUCCESS: 
                return "USBD_STATUS_SUCCESS";
        case USBD_STATUS_PORT_OPERATION_PENDING:
                return "USBD_STATUS_PORT_OPERATION_PENDING";
        case USBD_STATUS_PENDING:
                return "USBD_STATUS_PENDING";
        case USBD_STATUS_CRC:
                return "USBD_STATUS_CRC";
        case USBD_STATUS_BTSTUFF:
                return "USBD_STATUS_BTSTUFF";
        case USBD_STATUS_DATA_TOGGLE_MISMATCH:
                return "USBD_STATUS_DATA_TOGGLE_MISMATCH";
        case USBD_STATUS_STALL_PID:
                return "USBD_STATUS_STALL_PID";
        case USBD_STATUS_DEV_NOT_RESPONDING:
                return "USBD_STATUS_DEV_NOT_RESPONDING";
        case USBD_STATUS_PID_CHECK_FAILURE:
                return "USBD_STATUS_PID_CHECK_FAILURE";
        case USBD_STATUS_UNEXPECTED_PID:
                return "USBD_STATUS_UNEXPECTED_PID";
        case USBD_STATUS_DATA_OVERRUN:
                return "USBD_STATUS_DATA_OVERRUN";
        case USBD_STATUS_DATA_UNDERRUN:
                return "USBD_STATUS_DATA_UNDERRUN";
        case USBD_STATUS_BUFFER_OVERRUN:
                return "USBD_STATUS_BUFFER_OVERRUN";
        case USBD_STATUS_BUFFER_UNDERRUN:
                return "USBD_STATUS_BUFFER_UNDERRUN";
        case USBD_STATUS_NOT_ACCESSED:
                return "USBD_STATUS_NOT_ACCESSED";
        case USBD_STATUS_FIFO:
                return "USBD_STATUS_FIFO";
        case USBD_STATUS_XACT_ERROR:
                return "USBD_STATUS_XACT_ERROR";
        case USBD_STATUS_BABBLE_DETECTED:
                return "USBD_STATUS_BABBLE_DETECTED";
        case USBD_STATUS_DATA_BUFFER_ERROR:
                return "USBD_STATUS_DATA_BUFFER_ERROR";
        case USBD_STATUS_NO_PING_RESPONSE:
                return "USBD_STATUS_NO_PING_RESPONSE";
        case USBD_STATUS_INVALID_STREAM_TYPE:
                return "USBD_STATUS_INVALID_STREAM_TYPE";
        case USBD_STATUS_INVALID_STREAM_ID:
                return "USBD_STATUS_INVALID_STREAM_ID";
        case USBD_STATUS_ENDPOINT_HALTED:
                return "USBD_STATUS_ENDPOINT_HALTED";
        case USBD_STATUS_INVALID_URB_FUNCTION:
                return "USBD_STATUS_INVALID_URB_FUNCTION";
        case USBD_STATUS_INVALID_PARAMETER:
                return "USBD_STATUS_INVALID_PARAMETER";
        case USBD_STATUS_ERROR_BUSY:
                return "USBD_STATUS_ERROR_BUSY";
        case USBD_STATUS_INVALID_PIPE_HANDLE:
                return "USBD_STATUS_INVALID_PIPE_HANDLE";
        case USBD_STATUS_NO_BANDWIDTH:
                return "USBD_STATUS_NO_BANDWIDTH";
        case USBD_STATUS_INTERNAL_HC_ERROR:
                return "USBD_STATUS_INTERNAL_HC_ERROR";
        case USBD_STATUS_ERROR_SHORT_TRANSFER:
                return "USBD_STATUS_ERROR_SHORT_TRANSFER";
        case USBD_STATUS_BAD_START_FRAME:
                return "USBD_STATUS_BAD_START_FRAME";
        case USBD_STATUS_ISOCH_REQUEST_FAILED:
                return "USBD_STATUS_ISOCH_REQUEST_FAILED";
        case USBD_STATUS_FRAME_CONTROL_OWNED:
                return "USBD_STATUS_FRAME_CONTROL_OWNED";
        case USBD_STATUS_FRAME_CONTROL_NOT_OWNED:
                return "USBD_STATUS_FRAME_CONTROL_NOT_OWNED";
        case USBD_STATUS_NOT_SUPPORTED:
                return "USBD_STATUS_NOT_SUPPORTED";
        case USBD_STATUS_INAVLID_CONFIGURATION_DESCRIPTOR:
                return "USBD_STATUS_INAVLID_CONFIGURATION_DESCRIPTOR";
        case USBD_STATUS_INSUFFICIENT_RESOURCES:
                return "USBD_STATUS_INSUFFICIENT_RESOURCES";
        case USBD_STATUS_SET_CONFIG_FAILED:
                return "USBD_STATUS_SET_CONFIG_FAILED";
        case USBD_STATUS_BUFFER_TOO_SMALL:
                return "USBD_STATUS_BUFFER_TOO_SMALL";
        case USBD_STATUS_INTERFACE_NOT_FOUND:
                return "USBD_STATUS_INTERFACE_NOT_FOUND";
        case USBD_STATUS_INAVLID_PIPE_FLAGS:
                return "USBD_STATUS_INAVLID_PIPE_FLAGS";
        case USBD_STATUS_TIMEOUT:
                return "USBD_STATUS_TIMEOUT";
        case USBD_STATUS_DEVICE_GONE:
                return "USBD_STATUS_DEVICE_GONE";
        case USBD_STATUS_STATUS_NOT_MAPPED:
                return "USBD_STATUS_STATUS_NOT_MAPPED";
        case USBD_STATUS_HUB_INTERNAL_ERROR:
                return "USBD_STATUS_HUB_INTERNAL_ERROR";
        case USBD_STATUS_CANCELED:
                return "USBD_STATUS_CANCELED";
        case USBD_STATUS_ISO_NOT_ACCESSED_BY_HW:
                return "USBD_STATUS_ISO_NOT_ACCESSED_BY_HW";
        case USBD_STATUS_ISO_TD_ERROR:
                return "USBD_STATUS_ISO_TD_ERROR";
        case USBD_STATUS_ISO_NA_LATE_USBPORT:
                return "USBD_STATUS_ISO_NA_LATE_USBPORT";
        case USBD_STATUS_ISO_NOT_ACCESSED_LATE:
                return "USBD_STATUS_ISO_NOT_ACCESSED_LATE";
        case USBD_STATUS_BAD_DESCRIPTOR:
                return "USBD_STATUS_BAD_DESCRIPTOR";
        case USBD_STATUS_BAD_DESCRIPTOR_BLEN:
                return "USBD_STATUS_BAD_DESCRIPTOR_BLEN";
        case USBD_STATUS_BAD_DESCRIPTOR_TYPE:
                return "USBD_STATUS_BAD_DESCRIPTOR_TYPE";
        case USBD_STATUS_BAD_INTERFACE_DESCRIPTOR:
                return "USBD_STATUS_BAD_INTERFACE_DESCRIPTOR";
        case USBD_STATUS_BAD_ENDPOINT_DESCRIPTOR:
                return "USBD_STATUS_BAD_ENDPOINT_DESCRIPTOR";
        case USBD_STATUS_BAD_INTERFACE_ASSOC_DESCRIPTOR:
                return "USBD_STATUS_BAD_INTERFACE_ASSOC_DESCRIPTOR";
        case USBD_STATUS_BAD_CONFIG_DESC_LENGTH:
                return "USBD_STATUS_BAD_CONFIG_DESC_LENGTH";
        case USBD_STATUS_BAD_NUMBER_OF_INTERFACES:
                return "USBD_STATUS_BAD_NUMBER_OF_INTERFACES";
        case USBD_STATUS_BAD_NUMBER_OF_ENDPOINTS:
                return "USBD_STATUS_BAD_NUMBER_OF_ENDPOINTS";
        case USBD_STATUS_BAD_ENDPOINT_ADDRESS:
                return "USBD_STATUS_BAD_ENDPOINT_ADDRESS";
        }

        return "USBD_STATUS_?";
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

const char *dbg_usbip_hdr(const struct usbip_header *hdr)
{
	static char buf[512];

	int n = libdrv_snprintf(buf, sizeof(buf), "seq:%u,%s,ep:%u,cmd:%x", 
				hdr->base.seqnum, 
				hdr->base.direction ? "in" : "out", 
				hdr->base.ep, 
				hdr->base.command);

	int rest = sizeof(buf) - n;

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		libdrv_snprintf(buf + n, rest, "(submit),tlen:%d,intv:%d",
			hdr->u.cmd_submit.transfer_buffer_length, hdr->u.cmd_submit.interval);
		break;
	case USBIP_RET_SUBMIT:
		libdrv_snprintf(buf + n, rest, "(ret_submit),alen:%u", hdr->u.ret_submit.actual_length);
		break;
	case USBIP_CMD_UNLINK:
		libdrv_snprintf(buf + n, rest, "(unlink),unlinkseq:%u", hdr->u.cmd_unlink.seqnum);
		break;
	case USBIP_RET_UNLINK:
		libdrv_snprintf(buf + n, rest, "(ret_unlink),st:%u", hdr->u.ret_unlink.status);
		break;
	case USBIP_RESET_DEV:
		libdrv_snprintf(buf + n, rest, "(reset_dev)");
		break;
	}

	return buf;
}