/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbd_helper.h"

/*
 * Linux error codes.
 * See: include/uapi/asm-generic/errno-base.h, include/uapi/asm-generic/errno.h
 */
enum {
	ENOENT_LNX = 2,
	ENXIO_LNX = 6,
	ENOMEM_LNX = 12,
	EBUSY_LNX = 16,
	EXDEV_LNX = 18,
	ENODEV_LNX = 19,
	EINVAL_LNX = 22,
	ENOSPC_LNX = 28,
	EPIPE_LNX = 32,
	ETIME_LNX = 62,
	ENOSR_LNX = 63,
	ECOMM_LNX = 70,
	EPROTO_LNX = 71,
	EOVERFLOW_LNX = 75,
	EILSEQ_LNX = 84,
	ECONNRESET_LNX = 104,
	ESHUTDOWN_LNX = 108,
	ETIMEDOUT_LNX = 110,
	EHOSTUNREACH_LNX = 113,
	EINPROGRESS_LNX = 115,
	EREMOTEIO_LNX = 121,
};

/*
 * Status can be from usb_submit_urb or urb->status, we cannot know its origin.
 * Meaning of some errors differs for usb_submit_urb and urb->status, we would prefer urb->status.
 * See: https://www.kernel.org/doc/Documentation/usb/error-codes.txt
 */
USBD_STATUS to_windows_status_ex(int usbip_status, bool isoch)
{
	switch (usbip_status >= 0 ? usbip_status : -usbip_status) {
	case 0:
		return USBD_STATUS_SUCCESS;
	case EPIPE_LNX: // Endpoint stalled. For non-control endpoints, reset this status with usb_clear_halt()
		return EndpointStalled;
	case EREMOTEIO_LNX:
		return USBD_STATUS_ERROR_SHORT_TRANSFER;
	case ETIME_LNX:
		return USBD_STATUS_DEV_NOT_RESPONDING;
	case ETIMEDOUT_LNX:
		return USBD_STATUS_TIMEOUT;
	case ENOENT_LNX: // URB was synchronously unlinked by usb_unlink_urb
	case ECONNRESET_LNX: // URB was asynchronously unlinked by usb_unlink_urb
		return USBD_STATUS_CANCELED;
//	case EINPROGRESS_LNX:
//		return USBD_STATUS_PENDING; // don't send this to Windows
	case EOVERFLOW_LNX:
		return USBD_STATUS_BABBLE_DETECTED;
	case ENODEV_LNX:
	case ESHUTDOWN_LNX: // The device or host controller has been disabled
	case EHOSTUNREACH_LNX: // URB was rejected because the device is suspended
		return USBD_STATUS_DEVICE_GONE;
	case EILSEQ_LNX:
		return USBD_STATUS_CRC;
	case ECOMM_LNX:
		return USBD_STATUS_DATA_OVERRUN;
	case ENOSR_LNX:
		return USBD_STATUS_DATA_UNDERRUN;
	case ENOMEM_LNX:
		return USBD_STATUS_INSUFFICIENT_RESOURCES;
	case EPROTO_LNX:
		return USBD_STATUS_BTSTUFF; /* USBD_STATUS_INTERNAL_HC_ERROR */
	case ENOSPC_LNX:
		return USBD_STATUS_NO_BANDWIDTH;
	case EXDEV_LNX:
		return isoch ? USBD_STATUS_ISO_TD_ERROR : USBD_STATUS_ISOCH_REQUEST_FAILED;
	case ENXIO_LNX:
		return USBD_STATUS_INTERNAL_HC_ERROR;
	case EBUSY_LNX:
		return USBD_STATUS_ERROR_BUSY;
	}

	return USBD_STATUS_INVALID_PARAMETER;
}

int to_linux_status(USBD_STATUS status)
{
	int err = 0;

	switch (status) {
	case USBD_STATUS_SUCCESS:
		break;
	case EndpointStalled:
	case USBD_STATUS_ENDPOINT_HALTED:
		err = EPIPE_LNX;
		break;
	case USBD_STATUS_ERROR_SHORT_TRANSFER:
		err = EREMOTEIO_LNX;
		break;
	case USBD_STATUS_TIMEOUT:
		err = ETIMEDOUT_LNX; /* ETIME */
		break;
	case USBD_STATUS_CANCELED:
		err = ECONNRESET_LNX; /* ENOENT */
		break;
	case USBD_STATUS_PENDING:
		err = EINPROGRESS_LNX;
		break;
	case USBD_STATUS_BABBLE_DETECTED:
		err = EOVERFLOW_LNX;
		break;
	case USBD_STATUS_DEVICE_GONE:
		err = ENODEV_LNX;
		break;
	case USBD_STATUS_CRC:
		err = EILSEQ_LNX;
		break;
	case USBD_STATUS_DATA_OVERRUN:
		err = ECOMM_LNX;
		break;
	case USBD_STATUS_DATA_UNDERRUN:
		err = ENOSR_LNX;
		break;
	case USBD_STATUS_INSUFFICIENT_RESOURCES:
		err = ENOMEM_LNX;
		break;
	case USBD_STATUS_BTSTUFF:
	case USBD_STATUS_INTERNAL_HC_ERROR:
	case USBD_STATUS_HUB_INTERNAL_ERROR:
	case USBD_STATUS_DEV_NOT_RESPONDING:
		err = EPROTO_LNX;
		break;
	case USBD_STATUS_ERROR_BUSY:
		err = EBUSY_LNX;
		break;
	case USBD_STATUS_INVALID_PIPE_HANDLE:
		err = ENOENT_LNX;
		break;
	default:
		if (USBD_ERROR(status)) {
			err = EINVAL_LNX;
		}
	}

	return -err;
}

/*
* <linux/usb.h>, urb->transfer_flags
*/
enum {
	URB_SHORT_NOT_OK = 0x0001, // report short reads as errors
	URB_ISO_ASAP = 0x0002      // iso-only; use the first unexpired slot in the schedule
};

 /*
 TransferFlags
 Specifies zero, one, or a combination of the following flags: 

 USBD_TRANSFER_DIRECTION_IN
 Is set to request data from a device. To transfer data to a device, this flag must be clear.  

 USBD_SHORT_TRANSFER_OK
 This flag should not be set unless USBD_TRANSFER_DIRECTION_IN is also set.
 
 USBD_START_ISO_TRANSFER_ASAP
 Causes the transfer to begin on the next frame, if no transfers have been submitted to the pipe 
 since the pipe was opened or last reset. Otherwise, the transfer begins on the first frame that 
 follows all currently queued requests for the pipe. The actual frame that the transfer begins on 
 will be adjusted for bus latency by the host controller driver. 

 For control endpoints:
 1.Direction in endpoint address or transfer flags should be ignored
 2.Direction is determined by bits of bmRequestType in the Setup packet (D7 Data Phase Transfer Direction) 
 */
ULONG to_windows_flags(UINT32 transfer_flags, bool dir_in)
{
	ULONG TransferFlags = dir_in ? USBD_TRANSFER_DIRECTION_IN : USBD_TRANSFER_DIRECTION_OUT;

	if (dir_in && !(transfer_flags & URB_SHORT_NOT_OK)) {
		TransferFlags |= USBD_SHORT_TRANSFER_OK;
	}

	if (transfer_flags & URB_ISO_ASAP) {
		TransferFlags |= USBD_START_ISO_TRANSFER_ASAP;
	}

	return TransferFlags;
}

UINT32 to_linux_flags(ULONG TransferFlags, bool dir_in)
{
	UINT32 flags = 0;

	if (TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		flags |= URB_ISO_ASAP;
	} else if (dir_in && !(TransferFlags & USBD_SHORT_TRANSFER_OK)) {
		flags |= URB_SHORT_NOT_OK;
	}

	return flags;
}
