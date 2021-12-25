#include "dbgcommon.h"
#include "usbip_proto.h"
#include "usbip_vhci_api.h"

#include <usbdi.h>
#include <usbuser.h>
#include <ntstrsafe.h>

constexpr const char *bmrequest_dir_str(BM_REQUEST_TYPE r)
{
	return r.s.Dir == BMREQUEST_HOST_TO_DEVICE ? "OUT" : "IN";
}

const char *bmrequest_type_str(BM_REQUEST_TYPE r)
{
	static const char* v[] = { "STANDARD", "CLASS", "VENDOR", "BMREQUEST_3" };
	NT_ASSERT(r.s.Type < ARRAYSIZE(v));
	return v[r.s.Type];
}

const char *bmrequest_recipient_str(BM_REQUEST_TYPE r)
{
	static const char* v[] = { "DEVICE", "INTERFACE", "ENDPOINT", "OTHER" };
	NT_ASSERT(r.s.Recipient < ARRAYSIZE(v));
	return v[r.s.Recipient];
}

const char *brequest_str(UCHAR bRequest)
{
	switch (bRequest) {
	case USB_REQUEST_GET_STATUS: return "GET_STATUS";
	case USB_REQUEST_CLEAR_FEATURE: return "CLEAR_FEATURE";
	case USB_REQUEST_SET_FEATURE: return "SET_FEATURE";
	case USB_REQUEST_SET_ADDRESS: return "SET_ADDRESS";
	case USB_REQUEST_GET_DESCRIPTOR: return "GET_DESCRIPTOR";
	case USB_REQUEST_SET_DESCRIPTOR: return "SET_DESCRIPTOR";
	case USB_REQUEST_GET_CONFIGURATION: return "GET_CONFIGURATION";
	case USB_REQUEST_SET_CONFIGURATION: return "SET_CONFIGURATION";
	case USB_REQUEST_GET_INTERFACE: return "GET_INTERFACE";
	case USB_REQUEST_SET_INTERFACE: return "SET_INTERFACE";
	case USB_REQUEST_SYNC_FRAME: return "SYNC_FRAME";
	case USB_REQUEST_GET_FIRMWARE_STATUS: return "GET_FIRMWARE_STATUS";
	case USB_REQUEST_SET_FIRMWARE_STATUS: return "SET_FIRMWARE_STATUS";
	case USB_REQUEST_SET_SEL: return "SET_SEL";
	case USB_REQUEST_ISOCH_DELAY: return "ISOCH_DELAY";
	}

	return "USB_REQUEST_?";
}

const char *dbg_usbd_status(USBD_STATUS status)
{
        switch (status) {
        case USBD_STATUS_SUCCESS: 
                return "SUCCESS";
        case USBD_STATUS_PORT_OPERATION_PENDING:
                return "PORT_OPERATION_PENDING";
        case USBD_STATUS_PENDING:
                return "PENDING";
        case USBD_STATUS_CRC:
                return "CRC";
        case USBD_STATUS_BTSTUFF:
                return "BTSTUFF";
        case USBD_STATUS_DATA_TOGGLE_MISMATCH:
                return "DATA_TOGGLE_MISMATCH";
        case USBD_STATUS_STALL_PID:
                return "STALL_PID";
        case USBD_STATUS_DEV_NOT_RESPONDING:
                return "DEV_NOT_RESPONDING";
        case USBD_STATUS_PID_CHECK_FAILURE:
                return "PID_CHECK_FAILURE";
        case USBD_STATUS_UNEXPECTED_PID:
                return "UNEXPECTED_PID";
        case USBD_STATUS_DATA_OVERRUN:
                return "DATA_OVERRUN";
        case USBD_STATUS_DATA_UNDERRUN:
                return "DATA_UNDERRUN";
        case USBD_STATUS_BUFFER_OVERRUN:
                return "BUFFER_OVERRUN";
        case USBD_STATUS_BUFFER_UNDERRUN:
                return "BUFFER_UNDERRUN";
        case USBD_STATUS_NOT_ACCESSED:
                return "NOT_ACCESSED";
        case USBD_STATUS_FIFO:
                return "FIFO";
        case USBD_STATUS_XACT_ERROR:
                return "XACT_ERROR";
        case USBD_STATUS_BABBLE_DETECTED:
                return "BABBLE_DETECTED";
        case USBD_STATUS_DATA_BUFFER_ERROR:
                return "DATA_BUFFER_ERROR";
        case USBD_STATUS_NO_PING_RESPONSE:
                return "NO_PING_RESPONSE";
        case USBD_STATUS_INVALID_STREAM_TYPE:
                return "INVALID_STREAM_TYPE";
        case USBD_STATUS_INVALID_STREAM_ID:
                return "INVALID_STREAM_ID";
        case USBD_STATUS_ENDPOINT_HALTED:
                return "ENDPOINT_HALTED";
        case USBD_STATUS_INVALID_URB_FUNCTION:
                return "INVALID_URB_FUNCTION";
        case USBD_STATUS_INVALID_PARAMETER:
                return "INVALID_PARAMETER";
        case USBD_STATUS_ERROR_BUSY:
                return "ERROR_BUSY";
        case USBD_STATUS_INVALID_PIPE_HANDLE:
                return "INVALID_PIPE_HANDLE";
        case USBD_STATUS_NO_BANDWIDTH:
                return "NO_BANDWIDTH";
        case USBD_STATUS_INTERNAL_HC_ERROR:
                return "INTERNAL_HC_ERROR";
        case USBD_STATUS_ERROR_SHORT_TRANSFER:
                return "ERROR_SHORT_TRANSFER";
        case USBD_STATUS_BAD_START_FRAME:
                return "BAD_START_FRAME";
        case USBD_STATUS_ISOCH_REQUEST_FAILED:
                return "ISOCH_REQUEST_FAILED";
        case USBD_STATUS_FRAME_CONTROL_OWNED:
                return "FRAME_CONTROL_OWNED";
        case USBD_STATUS_FRAME_CONTROL_NOT_OWNED:
                return "FRAME_CONTROL_NOT_OWNED";
        case USBD_STATUS_NOT_SUPPORTED:
                return "NOT_SUPPORTED";
        case USBD_STATUS_INAVLID_CONFIGURATION_DESCRIPTOR:
                return "INAVLID_CONFIGURATION_DESCRIPTOR";
        case USBD_STATUS_INSUFFICIENT_RESOURCES:
                return "INSUFFICIENT_RESOURCES";
        case USBD_STATUS_SET_CONFIG_FAILED:
                return "SET_CONFIG_FAILED";
        case USBD_STATUS_BUFFER_TOO_SMALL:
                return "BUFFER_TOO_SMALL";
        case USBD_STATUS_INTERFACE_NOT_FOUND:
                return "INTERFACE_NOT_FOUND";
        case USBD_STATUS_INAVLID_PIPE_FLAGS:
                return "INAVLID_PIPE_FLAGS";
        case USBD_STATUS_TIMEOUT:
                return "TIMEOUT";
        case USBD_STATUS_DEVICE_GONE:
                return "DEVICE_GONE";
        case USBD_STATUS_STATUS_NOT_MAPPED:
                return "STATUS_NOT_MAPPED";
        case USBD_STATUS_HUB_INTERNAL_ERROR:
                return "HUB_INTERNAL_ERROR";
        case USBD_STATUS_CANCELED:
                return "CANCELED";
        case USBD_STATUS_ISO_NOT_ACCESSED_BY_HW:
                return "ISO_NOT_ACCESSED_BY_HW";
        case USBD_STATUS_ISO_TD_ERROR:
                return "ISO_TD_ERROR";
        case USBD_STATUS_ISO_NA_LATE_USBPORT:
                return "ISO_NA_LATE_USBPORT";
        case USBD_STATUS_ISO_NOT_ACCESSED_LATE:
                return "ISO_NOT_ACCESSED_LATE";
        case USBD_STATUS_BAD_DESCRIPTOR:
                return "BAD_DESCRIPTOR";
        case USBD_STATUS_BAD_DESCRIPTOR_BLEN:
                return "BAD_DESCRIPTOR_BLEN";
        case USBD_STATUS_BAD_DESCRIPTOR_TYPE:
                return "BAD_DESCRIPTOR_TYPE";
        case USBD_STATUS_BAD_INTERFACE_DESCRIPTOR:
                return "BAD_INTERFACE_DESCRIPTOR";
        case USBD_STATUS_BAD_ENDPOINT_DESCRIPTOR:
                return "BAD_ENDPOINT_DESCRIPTOR";
        case USBD_STATUS_BAD_INTERFACE_ASSOC_DESCRIPTOR:
                return "BAD_INTERFACE_ASSOC_DESCRIPTOR";
        case USBD_STATUS_BAD_CONFIG_DESC_LENGTH:
                return "BAD_CONFIG_DESC_LENGTH";
        case USBD_STATUS_BAD_NUMBER_OF_INTERFACES:
                return "BAD_NUMBER_OF_INTERFACES";
        case USBD_STATUS_BAD_NUMBER_OF_ENDPOINTS:
                return "BAD_NUMBER_OF_ENDPOINTS";
        case USBD_STATUS_BAD_ENDPOINT_ADDRESS:
                return "BAD_ENDPOINT_ADDRESS";
        }

        return "USBD_STATUS_?";
}

const char *dbg_ioctl_code(ULONG ioctl_code)
{
	static_assert(sizeof(ioctl_code) == sizeof(IOCTL_USBIP_VHCI_PLUGIN_HARDWARE), "assert");

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE: return "USBIP_VHCI_PLUGIN_HARDWARE";
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE: return "USBIP_VHCI_UNPLUG_HARDWARE";
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS: return "USBIP_VHCI_GET_PORTS_STATUS";
	case IOCTL_INTERNAL_USB_CYCLE_PORT: return "INTERNAL_USB_CYCLE_PORT";
	case IOCTL_INTERNAL_USB_ENABLE_PORT: return "INTERNAL_USB_ENABLE_PORT";
	case IOCTL_INTERNAL_USB_GET_BUS_INFO: return "INTERNAL_USB_GET_BUS_INFO";
	case IOCTL_INTERNAL_USB_GET_BUSGUID_INFO: return "INTERNAL_USB_GET_BUSGUID_INFO";
	case IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME: return "INTERNAL_USB_GET_CONTROLLER_NAME";
	case IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE: return "INTERNAL_USB_GET_DEVICE_HANDLE";
	case IOCTL_INTERNAL_USB_GET_HUB_COUNT: return "INTERNAL_USB_GET_HUB_COUNT";
	case IOCTL_INTERNAL_USB_GET_HUB_NAME: return "INTERNAL_USB_GET_HUB_NAME";
	case IOCTL_INTERNAL_USB_GET_PARENT_HUB_INFO: return "INTERNAL_USB_GET_PARENT_HUB_INFO";
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS: return "INTERNAL_USB_GET_PORT_STATUS";
	case IOCTL_INTERNAL_USB_RESET_PORT: return "INTERNAL_USB_RESET_PORT";
	case IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO: return "INTERNAL_USB_GET_ROOTHUB_PDO";
	case IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION: return "INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION";
	case IOCTL_INTERNAL_USB_SUBMIT_URB: return "INTERNAL_USB_SUBMIT_URB";
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS: return "INTERNAL_USB_GET_TOPOLOGY_ADDRESS";
	case IOCTL_USB_DIAG_IGNORE_HUBS_ON: return "USB_DIAG_IGNORE_HUBS_ON";
	case IOCTL_USB_DIAG_IGNORE_HUBS_OFF: return "USB_DIAG_IGNORE_HUBS_OFF";
	case IOCTL_USB_DIAGNOSTIC_MODE_OFF: return "USB_DIAGNOSTIC_MODE_OFF";
	case IOCTL_USB_DIAGNOSTIC_MODE_ON: return "USB_DIAGNOSTIC_MODE_ON";
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION: return "USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION";
	case IOCTL_USB_GET_HUB_CAPABILITIES: return "USB_GET_HUB_CAPABILITIES";
	case IOCTL_USB_GET_ROOT_HUB_NAME: return "USB_GET_ROOT_HUB_NAME";
//	case IOCTL_GET_HCD_DRIVERKEY_NAME: return "GET_HCD_DRIVERKEY_NAME";
//	case IOCTL_USB_GET_NODE_INFORMATION: return "USB_GET_NODE_INFORMATION";
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION: return "USB_GET_NODE_CONNECTION_INFORMATION";
	case IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES: return "USB_GET_NODE_CONNECTION_ATTRIBUTES";
	case IOCTL_USB_GET_NODE_CONNECTION_NAME: return "USB_GET_NODE_CONNECTION_NAME";
//	case IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME: return "USB_GET_NODE_CONNECTION_DRIVERKEY_NAME";
	case IOCTL_USB_HCD_DISABLE_PORT: return "USB_HCD_DISABLE_PORT";
	case IOCTL_USB_HCD_ENABLE_PORT: return "USB_HCD_ENABLE_PORT";
	case IOCTL_USB_HCD_GET_STATS_1: return "USB_HCD_GET_STATS_1";
//	case IOCTL_USB_HCD_GET_STATS_2: return "USB_HCD_GET_STATS_2";
	case IOCTL_USB_USER_REQUEST: return "USB_USER_REQUEST";
//	case IOCTL_USB_GET_HUB_CAPABILITIES: return "USB_GET_HUB_CAPABILITIES";
//	case IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES: return "USB_GET_NODE_CONNECTION_ATTRIBUTES";
	case IOCTL_USB_HUB_CYCLE_PORT: return "USB_HUB_CYCLE_PORT";
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX: return "USB_GET_NODE_CONNECTION_INFORMATION_EX";
	case IOCTL_USB_RESET_HUB: return "USB_RESET_HUB";
	case IOCTL_USB_GET_HUB_CAPABILITIES_EX: return "USB_GET_HUB_CAPABILITIES_EX";
//	case IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES: return "USB_GET_NODE_CONNECTION_ATTRIBUTES";
	case IOCTL_USB_GET_HUB_INFORMATION_EX: return "USB_GET_HUB_INFORMATION_EX";
	case IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES: return "USB_GET_PORT_CONNECTOR_PROPERTIES";
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2: return "USB_GET_NODE_CONNECTION_INFORMATION_EX_V2";
	}

	return "IOCTL_?";
}

static void print_cmd_submit(char *buf, size_t len, const struct usbip_header_cmd_submit *cmd)
{
	NTSTATUS st = RtlStringCbPrintfExA(buf, len,  &buf, &len, 0, 
			"cmd_submit: flags %#x, length %d, start_frame %d, isoc[%d], interval %d, ",
			cmd->transfer_flags, 
			cmd->transfer_buffer_length, 
			cmd->start_frame, 
			cmd->number_of_packets, 
			cmd->interval);

	if (!st) {
		usb_setup_pkt_str(buf, len, cmd->setup);
	}
}

static void print_ret_submit(char *buf, size_t len, const struct usbip_header_ret_submit *cmd)
{
	RtlStringCbPrintfA(buf, len, "ret_submit: status %d, actual_length %d, start_frame %d, isoc[%d], error_count %d", 
					cmd->status,
					cmd->actual_length,
					cmd->start_frame,
					cmd->number_of_packets,
					cmd->error_count);
}

const char *dbg_usbip_hdr(char *buf, size_t len, const struct usbip_header *hdr)
{
	if (!hdr) {
		return "usbip_header{null}";
	}

	const char *result = buf;
	const struct usbip_header_basic *base = &hdr->base;

	NTSTATUS st = RtlStringCbPrintfExA(buf, len, &buf, &len, 0, "{seqnum %u, devid %#x, %s[%u]}, ",
					base->seqnum, 
					base->devid,			
					base->direction == USBIP_DIR_OUT ? "out" : "in",
					base->ep);

	if (st != STATUS_SUCCESS) {
		return "dbg_usbip_hdr error";
	}

	switch (base->command) {
	case USBIP_CMD_SUBMIT:
		print_cmd_submit(buf, len, &hdr->u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		print_ret_submit(buf, len, &hdr->u.ret_submit);
		break;
	case USBIP_CMD_UNLINK:
		RtlStringCbPrintfA(buf, len, "cmd_unlink: seqnum %u", hdr->u.cmd_unlink.seqnum);
		break;
	case USBIP_RET_UNLINK:
		RtlStringCbPrintfA(buf, len, "ret_unlink: status %d", hdr->u.ret_unlink.status);
		break;
	case USBIP_RESET_DEV:
		RtlStringCbCopyA(buf, len, "reset_dev");
		break;
	default:
		RtlStringCbPrintfA(buf, len, "command %u", base->command);
	}

	return result;
}

const char *usb_setup_pkt_str(char *buf, size_t len, const void *packet)
{
	auto r  = static_cast<const USB_DEFAULT_PIPE_SETUP_PACKET*>(packet);

	NTSTATUS st = RtlStringCbPrintfA(buf, len, 
			"{%s|%s|%s, %s(%#02hhx), wValue %#04hx, wIndex %#04hx, wLength %#04hx(%d)}",
			bmrequest_dir_str(r->bmRequestType),
			bmrequest_type_str(r->bmRequestType),
			bmrequest_recipient_str(r->bmRequestType),
			brequest_str(r->bRequest),
			r->bRequest,
			r->wValue,
			r->wIndex, 
			r->wLength,
			r->wLength);

	return st != STATUS_INVALID_PARAMETER ? buf : "usb_setup_pkt_str invalid parameter";
}

const char* usbd_transfer_flags(char *buf, size_t len, ULONG TransferFlags)
{
	const char *dir = USBD_TRANSFER_DIRECTION(TransferFlags) == USBD_TRANSFER_DIRECTION_OUT ? "OUT" : "IN";

	NTSTATUS st = RtlStringCbPrintfA(buf, len, "%s%s%s%s", dir,
					TransferFlags & USBD_SHORT_TRANSFER_OK ? "|SHORT_OK" : "",
					TransferFlags & USBD_START_ISO_TRANSFER_ASAP ? "|ISO_ASAP" : "",
					TransferFlags & USBD_DEFAULT_PIPE_TRANSFER ? "|DEFAULT_PIPE" : "");

	return st != STATUS_INVALID_PARAMETER ? buf : "usbd_transfer_flags invalid parameter";
}

const char *usbd_pipe_type_str(USBD_PIPE_TYPE t)
{
	static const char* v[] = { "Ctrl", "Isoch", "Bulk", "Intr" };
	NT_ASSERT(t < ARRAYSIZE(v));
	return v[t];
}

/*
 * Can't use CUSTOM_TYPE(urb_function, ItemListShort(...)), it's too big for WPP.
 */
const char *urb_function_str(int function)
{
	static const char* v[] = 
	{
		"URB_SELECT_CONFIGURATION",
		"URB_SELECT_INTERFACE",
		"URB_FUNCTION_ABORT_PIPE",

		"URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL",
		"URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL",

		"URB_FUNCTION_GET_FRAME_LENGTH",
		"URB_FUNCTION_SET_FRAME_LENGTH",
		"URB_GET_CURRENT_FRAME_NUMBER",

		"URB_CONTROL_TRANSFER",
		"URB_BULK_OR_INTERRUPT_TRANSFER",
		"URB_ISOCH_TRANSFER",

		"URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE",
		"URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE",

		"URB_FUNCTION_SET_FEATURE_TO_DEVICE",
		"URB_FUNCTION_SET_FEATURE_TO_INTERFACE",
		"URB_FUNCTION_SET_FEATURE_TO_ENDPOINT",

		"URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE",
		"URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE",
		"URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT",

		"URB_FUNCTION_GET_STATUS_FROM_DEVICE",
		"URB_FUNCTION_GET_STATUS_FROM_INTERFACE",
		"URB_FUNCTION_GET_STATUS_FROM_ENDPOINT",

		"URB_FUNCTION_RESERVED_0X0016",       

		"URB_FUNCTION_VENDOR_DEVICE",
		"URB_FUNCTION_VENDOR_INTERFACE",
		"URB_FUNCTION_VENDOR_ENDPOINT",

		"URB_FUNCTION_CLASS_DEVICE",
		"URB_FUNCTION_CLASS_INTERFACE",
		"URB_FUNCTION_CLASS_ENDPOINT",

		"URB_FUNCTION_RESERVE_0X001D",

		"URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL",

		"URB_FUNCTION_CLASS_OTHER",
		"URB_FUNCTION_VENDOR_OTHER",

		"URB_FUNCTION_GET_STATUS_FROM_OTHER",

		"URB_FUNCTION_CLEAR_FEATURE_TO_OTHER",
		"URB_FUNCTION_SET_FEATURE_TO_OTHER",

		"URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT",
		"URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT",

		"URB_FUNCTION_GET_CONFIGURATION",
		"URB_FUNCTION_GET_INTERFACE",

		"URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE",
		"URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE",

		"URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR",

		"URB_FUNCTION_RESERVE_0X002B",
		"URB_FUNCTION_RESERVE_0X002C",
		"URB_FUNCTION_RESERVE_0X002D",
		"URB_FUNCTION_RESERVE_0X002E",
		"URB_FUNCTION_RESERVE_0X002F",

		"URB_FUNCTION_SYNC_RESET_PIPE",
		"URB_FUNCTION_SYNC_CLEAR_STALL",
		"URB_CONTROL_TRANSFER_EX",

		"URB_FUNCTION_RESERVE_0X0033",
		"URB_FUNCTION_RESERVE_0X0034 ",                 

		"URB_FUNCTION_OPEN_STATIC_STREAMS",
		"URB_FUNCTION_CLOSE_STATIC_STREAMS",
		"URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL",
		"URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL",

		"URB_FUNCTION_RESERVE_0X0039",
		"URB_FUNCTION_RESERVE_0X003A",        
		"URB_FUNCTION_RESERVE_0X003B",        
		"URB_FUNCTION_RESERVE_0X003C",        

		"URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS"
	};

	return function >= 0 && function < ARRAYSIZE(v) ? v[function] : "URB_FUNCTION_?";
}
