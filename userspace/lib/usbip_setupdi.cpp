#include "usbip_setupdi.h"
#include "usbip_common.h"

#include <windows.h>
#include <setupapi.h>
#include <cstdlib>


char *
get_dev_property(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD prop)
{
	DWORD length;

	if (!SetupDiGetDeviceRegistryProperty(dev_info, pdev_info_data, prop, nullptr, nullptr, 0, &length)) {
		auto err = GetLastError();
		switch (err) {
		case ERROR_INVALID_DATA:
			return _strdup("");
		case ERROR_INSUFFICIENT_BUFFER:
			break;
		default:
			dbg("failed to get device property: err: %x", err);
			return nullptr;
		}
	} else {
		dbg("unexpected case");
		return nullptr;
	}

        auto value = (char*)malloc(length);
	if (!value) {
		dbg("out of memory");
		return nullptr;
	}
	
        if (!SetupDiGetDeviceRegistryProperty(dev_info, pdev_info_data, prop, nullptr, (PBYTE)value, length, &length)) {
		dbg("failed to get device property: err: %x", GetLastError());
		free(value);
		return nullptr;
	}

        return value;
}

char *
get_id_hw(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_HARDWAREID);
}

char *
get_upper_filters(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_UPPERFILTERS);
}

char *
get_id_inst(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	*id_inst;
	DWORD	length;

	if (!SetupDiGetDeviceInstanceId(dev_info, pdev_info_data, nullptr, 0, &length)) {
		DWORD	err = GetLastError();
		if (err != ERROR_INSUFFICIENT_BUFFER) {
			dbg("get_id_inst: failed to get instance id: err: 0x%lx", err);
			return nullptr;
		}
	}
	else {
		dbg("get_id_inst: unexpected case");
		return nullptr;
	}
	id_inst = (char *)malloc(length);
	if (id_inst == nullptr) {
		dbg("get_id_inst: out of memory");
		return nullptr;
	}
	if (!SetupDiGetDeviceInstanceId(dev_info, pdev_info_data, id_inst, length, nullptr)) {
		dbg("failed to get instance id");
		free(id_inst);
		return nullptr;
	}
	return id_inst;
}

PSP_DEVICE_INTERFACE_DETAIL_DATA
get_intf_detail(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, LPCGUID pguid)
{
	SP_DEVICE_INTERFACE_DATA	dev_interface_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA	pdev_interface_detail;
	unsigned long len = 0;
	DWORD	err;

	dev_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	if (!SetupDiEnumDeviceInterfaces(dev_info, pdev_info_data, pguid, 0, &dev_interface_data)) {
		DWORD	err = GetLastError();
		if (err != ERROR_NO_MORE_ITEMS)
			dbg("SetupDiEnumDeviceInterfaces failed: err: 0x%lx", err);
		return nullptr;
	}
	SetupDiGetDeviceInterfaceDetail(dev_info, &dev_interface_data, nullptr, 0, &len, nullptr);
	err = GetLastError();
	if (err != ERROR_INSUFFICIENT_BUFFER) {
		dbg("SetupDiGetDeviceInterfaceDetail failed: err: 0x%lx", err);
		return nullptr;
	}

	// Allocate the required memory and set the cbSize.
	pdev_interface_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(len);
	if (pdev_interface_detail == nullptr) {
		dbg("can't malloc %lu size memory", len);
		return nullptr;
	}

	pdev_interface_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

	// Try to get device details.
	if (!SetupDiGetDeviceInterfaceDetail(dev_info, &dev_interface_data,
		pdev_interface_detail, len, &len, nullptr)) {
		// Errors.
		dbg("SetupDiGetDeviceInterfaceDetail failed: err: 0x%lx", GetLastError());
		free(pdev_interface_detail);
		return nullptr;
	}

	return pdev_interface_detail;
}

BOOL
set_device_state(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD state)
{
	SP_PROPCHANGE_PARAMS	prop_params;

	memset(&prop_params, 0, sizeof(SP_PROPCHANGE_PARAMS));

	prop_params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
	prop_params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
	prop_params.StateChange = state;
	prop_params.Scope = DICS_FLAG_CONFIGSPECIFIC;//DICS_FLAG_GLOBAL;
	prop_params.HwProfile = 0;

	if (!SetupDiSetClassInstallParams(dev_info, pdev_info_data, (SP_CLASSINSTALL_HEADER *)&prop_params, sizeof(SP_PROPCHANGE_PARAMS))) {
		dbg("failed to set class install parameters");
		return FALSE;
	}

	if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, dev_info, pdev_info_data)) {
		dbg("failed to call class installer");
		return FALSE;
	}

	return TRUE;
}

BOOL
restart_device(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	if (!set_device_state(dev_info, pdev_info_data, DICS_DISABLE)) {
		dbg("set_device_state DICS_DISABLE failed");
		return FALSE;
	}
	if (!set_device_state(dev_info, pdev_info_data, DICS_ENABLE)) {
		dbg("set_device_state DICS_ENABLE failed");
		return FALSE;
	}
	return TRUE;
}

static unsigned char
get_devno_from_inst_id(unsigned char devno_map[], const char *id_inst)
{
	unsigned char	devno = 0;
	int	ndevs;
	int	i;

	for (i = 0; id_inst[i]; i++) {
		devno += (unsigned char)(id_inst[i] * 19 + 13);
	}
	if (devno == 0)
		devno++;

	ndevs = 0;
	while (devno_map[devno - 1]) {
		if (devno == 255)
			devno = 1;
		else
			devno++;
		if (ndevs == 255) {
			/* devno map is full */
			return 0;
		}
		ndevs++;
	}
	devno_map[devno - 1] = 1;
	return devno;
}

static int
traverse_dev_info(HDEVINFO dev_info, walkfunc_t walker, void *ctx)
{
	SP_DEVINFO_DATA	dev_info_data;
	unsigned char	devno_map[255];
	int	idx;
	int	ret = 0;

	memset(devno_map, 0, 255);
	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
	for (idx = 0;; idx++) {
		char	*id_inst;
		devno_t	devno;

		if (!SetupDiEnumDeviceInfo(dev_info, idx, &dev_info_data)) {
			DWORD	err = GetLastError();

			if (err != ERROR_NO_MORE_ITEMS) {
				dbg("SetupDiEnumDeviceInfo failed to get device information: err: 0x%lx", err);
			}
			break;
		}
		id_inst = get_id_inst(dev_info, &dev_info_data);
		if (id_inst == nullptr)
			continue;
		devno = get_devno_from_inst_id(devno_map, id_inst);
		free(id_inst);
		if (devno == 0)
			continue;
		ret = walker(dev_info, &dev_info_data, devno, ctx);
		if (ret != 0)
			break;
	}

	SetupDiDestroyDeviceInfoList(dev_info);
	return ret;
}

int
traverse_usbdevs(walkfunc_t walker, BOOL present_only, void *ctx)
{
	HDEVINFO	dev_info;
	DWORD	flags = DIGCF_ALLCLASSES;

	if (present_only)
		flags |= DIGCF_PRESENT;
	dev_info = SetupDiGetClassDevs(nullptr, "USB", nullptr, flags);
	if (dev_info == INVALID_HANDLE_VALUE) {
		dbg("SetupDiGetClassDevs failed: 0x%lx", GetLastError());
		return ERR_GENERAL;
	}

	return traverse_dev_info(dev_info, walker, ctx);
}

int
traverse_intfdevs(walkfunc_t walker, LPCGUID pguid, void *ctx)
{
	HDEVINFO	dev_info;

	dev_info = SetupDiGetClassDevs(pguid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		dbg("SetupDiGetClassDevs failed: 0x%lx", GetLastError());
		return ERR_GENERAL;
	}

	return traverse_dev_info(dev_info, walker, ctx);
}

static BOOL
is_valid_usb_dev(const char *id_hw)
{
	/*
	 * A valid USB hardware identifer(stub or vhci accepts) has one of following patterns:
	 * - USB\VID_0000&PID_0000&REV_0000
	 * - USB\VID_0000&PID_0000 (Is it needed?)
	 * Hardware ids of multi-interface device are not valid such as USB\VID_0000&PID_0000&MI_00.
	 */
	if (id_hw == nullptr)
		return FALSE;
	if (strncmp(id_hw, "USB\\", 4) != 0)
		return FALSE;
	if (strncmp(id_hw + 4, "VID_", 4) != 0)
		return FALSE;
	if (strlen(id_hw + 8) < 4)
		return FALSE;
	if (strncmp(id_hw + 12, "&PID_", 5) != 0)
		return FALSE;
	if (strlen(id_hw + 17) < 4)
		return FALSE;
	if (id_hw[21] == '\0')
		return TRUE;
	if (strncmp(id_hw + 21, "&REV_", 5) != 0)
		return FALSE;
	if (strlen(id_hw + 26) < 4)
		return FALSE;
	if (id_hw[30] != '\0')
		return FALSE;
	return TRUE;
}

BOOL
get_usbdev_info(const char *cid_hw, unsigned short *pvendor, unsigned short *pproduct)
{
	char	*id_hw = _strdup(cid_hw);

	if (!is_valid_usb_dev(id_hw))
		return FALSE;
	id_hw[12] = '\0';
	sscanf_s(id_hw + 8, "%hx", pvendor);
	id_hw[21] = '\0';
	sscanf_s(id_hw + 17, "%hx", pproduct);
	return TRUE;
}

devno_t
get_devno_from_busid(const char *busid)
{
	unsigned	busno;
	devno_t		devno;

	if (sscanf_s(busid, "%u-%hhu", &busno, &devno) != 2) {
		dbg("invalid busid: %s", busid);
		return 0;
	}
	if (busno != 1) {
		dbg("invalid busid: %s", busid);
		return 0;
	}
	return devno;
}
