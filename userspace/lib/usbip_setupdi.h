#pragma once

#include <windows.h>
#include <setupapi.h>

typedef unsigned char devno_t;
using walkfunc_t = int(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx);

int traverse_usbdevs(walkfunc_t walker, BOOL present_only, void *ctx);
int traverse_intfdevs(walkfunc_t walker, LPCGUID pguid, void *ctx);

char *get_id_hw(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data);

SP_DEVICE_INTERFACE_DETAIL_DATA *get_intf_detail(HDEVINFO dev_info, SP_DEVINFO_DATA *pdev_info_data, LPCGUID pguid);

BOOL get_usbdev_info(const char *id_hw, unsigned short *pvendor, unsigned short *pproduct);
