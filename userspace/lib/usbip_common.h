/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "usbip_api_consts.h"

#include <stdint.h>
#include <stdio.h>

extern int usbip_use_stderr;
extern int usbip_use_debug ;

extern const char *usbip_progname;

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


#include <PSHPACK1.H>

struct usbip_usb_interface 
{
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t padding;	/* alignment */
};

struct usbip_usb_device 
{
	char path[USBIP_DEV_PATH_MAX];
	char busid[USBIP_BUS_ID_SIZE];

	uint32_t busnum;
	uint32_t devnum;
	uint32_t speed;

	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;

	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bConfigurationValue;
	uint8_t bNumConfigurations;
	uint8_t bNumInterfaces;
};

#include <POPPACK.H>

void dump_usb_interface(struct usbip_usb_interface *);
void dump_usb_device(struct usbip_usb_device *);

const char *usbip_speed_string(enum usb_device_speed speed);
const char *usbip_status_string(enum usbip_device_status status);

int usbip_names_init();
void usbip_names_free();

void usbip_names_get_product(char *buff, size_t size, uint16_t vendor, uint16_t product);
void usbip_names_get_class(char *buff, size_t size, uint8_t class_, uint8_t subclass, uint8_t protocol);

#ifdef __cplusplus
}
#endif
