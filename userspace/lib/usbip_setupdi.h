#pragma once

#include <windows.h>
#include <setupapi.h>

typedef unsigned char	devno_t;

typedef int (*walkfunc_t)(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx);

int traverse_usbdevs(walkfunc_t walker, BOOL present_only, void *ctx);
int traverse_intfdevs(walkfunc_t walker, LPCGUID pguid, void *ctx);

char *get_id_hw(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data);
char *get_upper_filters(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data);
char *get_id_inst(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data);
PSP_DEVICE_INTERFACE_DETAIL_DATA get_intf_detail(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, LPCGUID pguid);

devno_t get_devno_from_busid(const char *busid);

BOOL get_usbdev_info(const char *id_hw, unsigned short *pvendor, unsigned short *pproduct);
BOOL set_device_state(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD state);
BOOL restart_device(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data);
