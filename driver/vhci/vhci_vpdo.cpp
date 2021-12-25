#include "vhci_vpdo.h"
#include "trace.h"
#include "vhci_vpdo.tmh"

#include "vhci.h"
#include "usbreq.h"
#include "devconf.h"

PAGEABLE NTSTATUS vpdo_select_config(vpdo_dev_t *vpdo, struct _URB_SELECT_CONFIGURATION *r)
{
	PAGED_CODE();

	if (vpdo->actconfig) {
		ExFreePoolWithTag(vpdo->actconfig, USBIP_VHCI_POOL_TAG);
		vpdo->actconfig = nullptr;
	}

	USB_CONFIGURATION_DESCRIPTOR *cd = r->ConfigurationDescriptor;
	if (!cd) {
		TraceInfo(TRACE_VPDO, "Going to unconfigured state");
		return STATUS_SUCCESS;
	}

	vpdo->actconfig = (USB_CONFIGURATION_DESCRIPTOR*)ExAllocatePoolWithTag(NonPagedPool, cd->wTotalLength, USBIP_VHCI_POOL_TAG);

	if (vpdo->actconfig) {
		RtlCopyMemory(vpdo->actconfig, cd, cd->wTotalLength);
	} else {
		TraceError(TRACE_VPDO, "Failed to allocate configuration descriptor");
		return STATUS_UNSUCCESSFUL;
	}

	NTSTATUS status = setup_config(r, vpdo->speed);

	if (NT_SUCCESS(status)) {
		r->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)(0x100 | cd->bConfigurationValue);

		char buf[SELECT_CONFIGURATION_STR_BUFSZ];
		TraceInfo(TRACE_VPDO, "%s", select_configuration_str(buf, sizeof(buf), r));
	}

	return status;
}

PAGEABLE NTSTATUS vpdo_select_interface(vpdo_dev_t *vpdo, struct _URB_SELECT_INTERFACE *r)
{
	PAGED_CODE();

	if (!vpdo->actconfig) {
		TraceError(TRACE_VPDO, "Device is unconfigured");
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	USBD_INTERFACE_INFORMATION *iface = &r->Interface;
	NTSTATUS status = setup_intf(iface, vpdo->speed, vpdo->actconfig);

	if (NT_SUCCESS(status)) {
		char buf[SELECT_INTERFACE_STR_BUFSZ];
		TraceInfo(TRACE_VPDO, "%s", select_interface_str(buf, sizeof(buf), r));

		vpdo->current_intf_num = iface->InterfaceNumber;
		vpdo->current_intf_alt = iface->AlternateSetting;
	}

	return status;
}

static PAGEABLE bool copy_ep(int i, USB_ENDPOINT_DESCRIPTOR *d, void *data)
{
	PAGED_CODE();

	USB_PIPE_INFO *pi = (USB_PIPE_INFO*)data + i;

	RtlCopyMemory(&pi->EndpointDescriptor, d, sizeof(*d));
	pi->ScheduleOffset = 0; // TODO

	return false;
}

PAGEABLE NTSTATUS vpdo_get_nodeconn_info(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION conninfo, PULONG poutlen)
{
	PAGED_CODE();

	ULONG outlen = 0;
	NTSTATUS status = STATUS_INVALID_PARAMETER;

	conninfo->DeviceAddress = (USHORT)conninfo->ConnectionIndex;
	conninfo->NumberOfOpenPipes = 0;
	conninfo->DeviceIsHub = FALSE;

	if (vpdo == nullptr) {
		conninfo->ConnectionStatus = NoDeviceConnected;
		conninfo->LowSpeed = FALSE;
		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION);
		status = STATUS_SUCCESS;
	} else {
		if (!vpdo->dsc_dev) {
			return STATUS_INVALID_PARAMETER;
		}

		conninfo->ConnectionStatus = DeviceConnected;

		RtlCopyMemory(&conninfo->DeviceDescriptor, vpdo->dsc_dev, sizeof(*vpdo->dsc_dev));

		if (vpdo->actconfig) {
			conninfo->CurrentConfigurationValue = vpdo->actconfig->bConfigurationValue;
		}

		conninfo->LowSpeed = vpdo->speed == USB_SPEED_LOW || vpdo->speed == USB_SPEED_FULL;

		USB_INTERFACE_DESCRIPTOR *dsc_intf = dsc_find_intf(vpdo->actconfig, vpdo->current_intf_num, vpdo->current_intf_alt);
		if (dsc_intf) {
			conninfo->NumberOfOpenPipes = dsc_intf->bNumEndpoints;
		}

		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(USB_PIPE_INFO) * conninfo->NumberOfOpenPipes;
		if (*poutlen < outlen) {
			status = STATUS_BUFFER_TOO_SMALL;
		} else {
			if (conninfo->NumberOfOpenPipes > 0) {
				dsc_for_each_endpoint(vpdo->actconfig, dsc_intf, copy_ep, conninfo->PipeList);
			}
			status = STATUS_SUCCESS;
		}
	}

	*poutlen = outlen;
	return status;
}

PAGEABLE NTSTATUS vpdo_get_nodeconn_info_ex(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX conninfo, PULONG poutlen)
{
	PAGED_CODE();

	ULONG outlen = 0;
	NTSTATUS status = STATUS_INVALID_PARAMETER;

	conninfo->DeviceAddress = (USHORT)conninfo->ConnectionIndex;
	conninfo->NumberOfOpenPipes = 0;
	conninfo->DeviceIsHub = FALSE;

	if (!vpdo) {
		conninfo->ConnectionStatus = NoDeviceConnected;
		conninfo->Speed = UsbFullSpeed;
		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION);
		status = STATUS_SUCCESS;
	} else {
		if (!vpdo->dsc_dev) {
			return STATUS_INVALID_PARAMETER;
		}

		conninfo->ConnectionStatus = DeviceConnected;
		RtlCopyMemory(&conninfo->DeviceDescriptor, vpdo->dsc_dev, sizeof(USB_DEVICE_DESCRIPTOR));

		if (vpdo->actconfig) {
			conninfo->CurrentConfigurationValue = vpdo->actconfig->bConfigurationValue;
		}

		conninfo->Speed = static_cast<UCHAR>(vpdo->speed);

		auto dsc_intf = dsc_find_intf(vpdo->actconfig, vpdo->current_intf_num, vpdo->current_intf_alt);
		if (dsc_intf) {
			conninfo->NumberOfOpenPipes = dsc_intf->bNumEndpoints;
		}

		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(USB_PIPE_INFO) * conninfo->NumberOfOpenPipes;
		if (*poutlen < outlen) {
			status = STATUS_BUFFER_TOO_SMALL;
		} else {
			if (conninfo->NumberOfOpenPipes > 0) {
				dsc_for_each_endpoint(vpdo->actconfig, dsc_intf, copy_ep, conninfo->PipeList);
			}
			status = STATUS_SUCCESS;
		}
	}

	*poutlen = outlen;
	return status;
}

PAGEABLE NTSTATUS vpdo_get_nodeconn_info_ex_v2(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX_V2 conninfo, PULONG poutlen)
{
	PAGED_CODE();
	UNREFERENCED_PARAMETER(vpdo);

	conninfo->SupportedUsbProtocols.ul = 0;
	conninfo->SupportedUsbProtocols.Usb110 = TRUE;
	conninfo->SupportedUsbProtocols.Usb200 = TRUE;
	conninfo->Flags.ul = 0;
	conninfo->Flags.DeviceIsOperatingAtSuperSpeedOrHigher = FALSE;
	conninfo->Flags.DeviceIsSuperSpeedCapableOrHigher = FALSE;
	conninfo->Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher = FALSE;
	conninfo->Flags.DeviceIsSuperSpeedPlusCapableOrHigher = FALSE;

	*poutlen = sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2);

	return STATUS_SUCCESS;
}
