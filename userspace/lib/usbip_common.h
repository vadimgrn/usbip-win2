/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#pragma once

#include "ch9.h"
#include "usbip_api_consts.h"
#include "usbip_proto.h"
#include "usb_ids.h"

#include <cstdint>
#include <cstdio>
#include <string>

extern int usbip_use_stderr;
extern int usbip_use_debug ;

extern const char *usbip_progname;

class UsbIds;

#define pr_fmt(fmt)	"%s: %s: " fmt "\n", usbip_progname
#define dbg_fmt(fmt)	pr_fmt("%s:%d:[%s] " fmt), "debug",	\
		        strrchr(__FILE__, '\\') + 1, __LINE__, __func__

#define err(fmt, ...)								\
	do {									\
		if (usbip_use_stderr) {						\
			fprintf(stderr, pr_fmt(fmt), "error", ##__VA_ARGS__);	\
		}								\
	} while (0)

#define info(fmt, ...)								\
	do {									\
		if (usbip_use_stderr) {						\
			fprintf(stderr, pr_fmt(fmt), "info", ##__VA_ARGS__);	\
		}								\
	} while (0)

#define dbg(fmt, ...)								\
	do {									\
		if (usbip_use_debug) {						\
			if (usbip_use_stderr) {					\
				fprintf(stderr, dbg_fmt(fmt), ##__VA_ARGS__);	\
			}							\
		}								\
	} while (0)


struct usbip_usb_interface;
struct usbip_usb_device;

void dump_usb_interface(usbip_usb_interface*);
void dump_usb_device(usbip_usb_device*);

const char *usbip_speed_string(usb_device_speed speed);
const char *usbip_status_string(usbip_device_status status);

std::string usbip_names_get_product(const UsbIds &ids, uint16_t vendor, uint16_t product);
std::string usbip_names_get_class(const UsbIds &ids, uint8_t class_, uint8_t subclass, uint8_t protocol);
