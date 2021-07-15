#pragma once

#include "usbip_proto.h" 
#include <usbdi.h>

NTSTATUS
setup_config(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PUSBD_INTERFACE_INFORMATION info_intf, PVOID end_info_intf, enum usb_device_speed speed);

NTSTATUS
setup_intf(USBD_INTERFACE_INFORMATION *intf_info, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, enum usb_device_speed speed);
