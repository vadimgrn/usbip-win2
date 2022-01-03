#pragma once

#include <ntddk.h>
#include <usbspec.h>

#include "usbdsc.h"

#define INFO_INTF_SIZE(info_intf)	(sizeof(USBD_INTERFACE_INFORMATION) + ((info_intf)->NumberOfPipes - 1) * sizeof(USBD_PIPE_INFORMATION))

typedef struct {
	UCHAR	bConfigurationValue;
	UCHAR	bNumInterfaces;
	USBD_CONFIGURATION_HANDLE	hConf;
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf;
	PUSBD_INTERFACE_INFORMATION	infos_intf[1];
} devconf_t;

devconf_t *create_devconf(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, USBD_CONFIGURATION_HANDLE hconf, PUSBD_INTERFACE_LIST_ENTRY pintf_list);
void free_devconf(devconf_t *devconf);
void update_devconf(devconf_t *devconf, PUSBD_INTERFACE_INFORMATION info_intf);

ULONG get_info_intf_size(devconf_t *devconf, UCHAR intf_num, UCHAR alt_setting);
PUSBD_PIPE_INFORMATION get_info_pipe(devconf_t *devconf, UCHAR epaddr);

enum { DBG_INFO_INTF_BUFSZ = 32 };
const char *dbg_info_intf(char *buf, unsigned int len, const USBD_INTERFACE_INFORMATION *info_intf);

enum { DBG_INFO_PIPE_BUFSZ = 32 };
const char *dbg_info_pipe(char *buf, unsigned int len, const USBD_PIPE_INFORMATION *info_pipe);