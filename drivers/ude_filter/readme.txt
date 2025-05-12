This is a device-specific upper filter driver which is required for Emulated Host Controller driver usbip2_ude.
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

###

The issue of this implementation is that the driver install/delete restarts all hubs:
     dvi:                     Restart required for any devices using this driver.
     dvi:                     Install Device: Configuring device. 11:09:57.122
     dvi:                          Configuration: usbhub3.inf:USB\ROOT_HUB30,Generic.Install.NT
     dvi:                          Configuration: oem137.inf:USB\ROOT_HUB30,*
     dvi:                     Install Device: Configuring device completed. 11:09:57.122
     dvi:                     Device Status: 0x0180200a
     dvi:                     {Restarting Devices} 11:09:57.122
     dvi:                          Needs Reinstall: USB\VID_07CA&PID_513C\MSFT201562DD065C
     dvi:                          Query-remove: USB\ROOT_HUB30\7&17CE9A26&0&0
     dvi:                          Query-remove: USB\ROOT_HUB30\5&15B1CE00&0&0
     dvi:                          Query-remove: USB\ROOT_HUB30\4&62F4A8E&0&0
!    dvi:                          Query-removal vetoed by 'USB\VID_8087&PID_0026\5&11277e5d&0&14??' (veto type 5: PNP_VetoOutstandingOpen).
!    dvi:                          Device required reboot: Query remove failed : 0x17: CR_REMOVE_VETOED.
     dvi:                          Start: USB\ROOT_HUB30\7&17CE9A26&0&0
     dvi:                          Start: USB\ROOT_HUB30\5&15B1CE00&0&0
     dvi:                          Start: USB\ROOT_HUB30\4&62F4A8E&0&0
     dvi:                     {Restarting Devices exit} 11:10:05.453

If set HWID of particular USB Hub (f.e. USB\ROOT_HUB30&VID8086&PID15EC&REV0006),
all hubs will be restarted anyway. See message "Restart required for any devices using this driver").

Set upper filter for Emulated HC only is a good idea, but it is not possible to preprocess PNP request
to modify its HWID. WdfDeviceInitAssignWdmIrpPreprocessCallback will return STATUS_INVALID_DEVICE_REQUEST
if pass IRP_MJ_PNP/IRP_MN_QUERY_ID. If pass IRP_MJ_PNP only, the callback will never be called.

### Alternative implementation

Commit "Use AddFilter directive instead of AddReg/DelReg", af6715703d71a3acce9039fee78fb366ae9256f9
It installs an upper filter for all Class=USB devices. 
The critical issue is the same as for the first solution, but there are more affected devices:
     dvi:           Class filters modified, class devices must be restarted.
     dvi:      Restarting all devices in {36fc9e60-c465-11cf-8056-444553540000} class.
     dvi:      {Restarting Devices} 09:27:09.913
     dvi:           Query-remove: PCI\VEN_10DE&DEV_1AED&SUBSYS_878D103C&REV_A1\4&B4D3340&0&0308
     dvi:           Query-remove: PCI\VEN_8086&DEV_06ED&SUBSYS_878D103C&REV_00\3&11583659&0&A0
!    dvi:           Query-removal vetoed by 'USB\VID_8087&PID_0026\5&11277e5d&0&14' (veto type 5: PNP_VetoOutstandingOpen).
!    dvi:           Device required reboot: Query remove failed : 0x17: CR_REMOVE_VETOED.
     dvi:           Query-remove: USB\ROOT_HUB30\7&17CE9A26&0&0
     dvi:           Query-remove: PCI\VEN_10DE&DEV_1AEC&SUBSYS_878D103C&REV_A1\4&B4D3340&0&0208
     dvi:           Query-remove: USB\ROOT_HUB30\5&15B1CE00&0&0
     dvi:           Query-remove: PCI\VEN_8086&DEV_15EC&SUBSYS_878D103C&REV_06\B477B42905C9A00000
     dvi:           Query-remove: USB\ROOT_HUB30\4&62F4A8E&0&0
     dvi:           Query-remove: USB\VID_0408&PID_5429\0001
     dvi:           Start: PCI\VEN_10DE&DEV_1AED&SUBSYS_878D103C&REV_A1\4&B4D3340&0&0308
     dvi:           Start: PCI\VEN_8086&DEV_06ED&SUBSYS_878D103C&REV_00\3&11583659&0&A0
     dvi:           Start: PCI\VEN_10DE&DEV_1AEC&SUBSYS_878D103C&REV_A1\4&B4D3340&0&0208
     dvi:           Start: USB\ROOT_HUB30\5&15B1CE00&0&0
     dvi:           Start: PCI\VEN_8086&DEV_15EC&SUBSYS_878D103C&REV_06\B477B42905C9A00000
     dvi:           Start: USB\ROOT_HUB30\4&62F4A8E&0&0
     dvi:      {Restarting Devices exit} 09:27:20.575
!    dvi:      Reboot required to restart all devices in class.
