/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2022-2023 Vadym Hrynchyshyn
 */

#pragma once

#include "usb_ids.h"
#include <usbip\ch9.h>

class UsbIds;

struct usbip_usb_interface;
struct usbip_usb_device;

void dump_usb_interface(usbip_usb_interface*);
void dump_usb_device(usbip_usb_device*);

const char *usbip_speed_string(usb_device_speed speed);

std::string usbip_names_get_product(const UsbIds &ids, uint16_t vendor, uint16_t product);
std::string usbip_names_get_class(const UsbIds &ids, uint8_t class_, uint8_t subclass, uint8_t protocol);
