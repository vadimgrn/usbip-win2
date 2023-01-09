This is an upper filter driver which is required for emulated HCI (VHCI) driver usbip2_vhci.
UDE doesn't propagate SELECT_CONFIGURATION/SELECT_INTERFACE and this is a major issue for VHCI.
This driver fixes that. 

Driver's class is "USB host controllers and USB hubs". 
This means its DRIVER_ADD_DEVICE routine will be called by PnP Manager for such PDO-s.
The driver creates Filter Device Object for PDO of USB Root Hub 3.0 which is created above VHCI.

It catches IRP_MJ_PNP -> IRP_MN_QUERY_DEVICE_RELATIONS for this hub.
Fow each newly added PDO (which is an emulated usb device) it creates Filter Device Object and catches
IRP_MJ_INTERNAL_DEVICE_CONTROL -> IOCTL_INTERNAL_USB_SUBMIT_URB -> URB_FUNCTION_SELECT_CONFIGURATION | URB_FUNCTION_SELECT_INTERFACE.

For each such request it creates _URB_CONTROL_TRANSFER_EX and passes it down.
In such way usbip2_vhci driver receives required information.