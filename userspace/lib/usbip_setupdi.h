#pragma once

#include <windows.h>
#include <setupapi.h>

#include <memory>
#include <string>

using devno_t = UCHAR;
using walkfunc_t = int(HDEVINFO dev_info, SP_DEVINFO_DATA *pdev_info_data, devno_t devno, void *ctx);

int traverse_intfdevs(walkfunc_t walker, const GUID &guid, void *ctx);

std::string get_id_hw(HDEVINFO dev_info, SP_DEVINFO_DATA *pdev_info_data);
std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> get_intf_detail(HDEVINFO dev_info, SP_DEVINFO_DATA *pdev_info_data, const GUID &pguid);
