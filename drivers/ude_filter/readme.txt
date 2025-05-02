This is an device-specific upper filter driver which is required for Emulated Host Controller driver usbip2_ude.
UDE doesn't propagate correctly SELECT_CONFIGURATION/SELECT_INTERFACE and this is a major issue.
This driver fixes that. 

DRIVER_ADD_DEVICE routine will be called by PnP Manager for each PDO with HWID USB\ROOT_HUB30.
The driver creates Filter Device Object for PDO located on Emulated Host Controller.
It does not create FDOs for USB Hubs located on other USB Host Controllers.

The device stack of "USB Root Hub (USB 3.0)" located on "USBip 3.X Emulated Host Controller":
\Driver\usbip2_filter
\Driver\USBHUB3
\Driver\usbip2_ude

It catches IRP_MJ_PNP -> IRP_MN_QUERY_DEVICE_RELATIONS for this hub.
Fow each newly added PDO (which is an emulated usb device) it creates Filter Device Object and catches
IRP_MJ_INTERNAL_DEVICE_CONTROL -> IOCTL_INTERNAL_USB_SUBMIT_URB -> URB_FUNCTION_SELECT_CONFIGURATION | URB_FUNCTION_SELECT_INTERFACE.

For each such request it creates _URB_CONTROL_TRANSFER_EX and passes it down.
In such way usbip2_ude driver receives required information.