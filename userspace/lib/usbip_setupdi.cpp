#include "usbip_setupdi.h"
#include "usbip_common.h"

#include <cstdlib>

namespace
{

using DeviceInfoListPtr = std::unique_ptr<void, decltype(SetupDiDestroyDeviceInfoList)&>;

auto get_id_inst(HDEVINFO dev_info, SP_DEVINFO_DATA *pdev_info_data)
{
	std::string str;

	DWORD length;
	if (!SetupDiGetDeviceInstanceId(dev_info, pdev_info_data, nullptr, 0, &length)) {
		auto err = GetLastError();
		if (err != ERROR_INSUFFICIENT_BUFFER) {
			dbg("get_id_inst: failed to get instance id: err: 0x%lx", err);
			return str;
		}
	} else {
		dbg("get_id_inst: unexpected case");
		return str;
	}

	auto val = std::make_unique_for_overwrite<char[]>(length);

	if (!SetupDiGetDeviceInstanceId(dev_info, pdev_info_data, val.get(), length, nullptr)) {
		dbg("failed to get instance id");
		return str;
	}

	str.assign(val.get(), length);
	return str;
}

std::string get_dev_property(HDEVINFO dev_info, SP_DEVINFO_DATA *pdev_info_data, DWORD prop)
{
	std::string str;

	DWORD length;
	if (!SetupDiGetDeviceRegistryProperty(dev_info, pdev_info_data, prop, nullptr, nullptr, 0, &length)) {
		switch (auto err = GetLastError()) {
		case ERROR_INVALID_DATA:
			return str;
		case ERROR_INSUFFICIENT_BUFFER:
			break;
		default:
			dbg("failed to get device property: err: %x", err);
			return str;
		}
	} else {
		dbg("unexpected case");
		return str;
	}

	auto val = std::make_unique_for_overwrite<BYTE[]>(length);
	
        if (!SetupDiGetDeviceRegistryProperty(dev_info, pdev_info_data, prop, nullptr, val.get(), length, nullptr)) {
		dbg("failed to get device property: err: %x", GetLastError());
		return str;
	}

	str.assign((char*)val.get(), length);
        return str;
}

unsigned char get_devno_from_inst_id(unsigned char devno_map[], const std::string &id_inst)
{
	UCHAR devno = 0;

	for (auto i: id_inst) {
		devno += (unsigned char)(i*19 + 13); // FIXME: ???
	}
	if (!devno) {
		++devno;
	}

	int ndevs = 0;
	while (devno_map[devno - 1]) {
		if (devno == UCHAR_MAX) {
			devno = 1;
		} else {
			devno++;
		}
		if (ndevs == UCHAR_MAX) {
			/* devno map is full */
			return 0;
		}
		ndevs++;
	}
	devno_map[devno - 1] = 1;
	return devno;
}

int traverse_dev_info(HDEVINFO dev_info, walkfunc_t walker, void *ctx)
{
	unsigned char devno_map[UCHAR_MAX]{};
	int ret = 0;

	SP_DEVINFO_DATA	dev_info_data{ sizeof(dev_info_data) };

	for (int idx = 0; !ret; ++idx) {
		if (!SetupDiEnumDeviceInfo(dev_info, idx, &dev_info_data)) {
			auto err = GetLastError();
			if (err != ERROR_NO_MORE_ITEMS) {
				dbg("SetupDiEnumDeviceInfo failed to get device information: err: 0x%lx", err);
			}
			break;
		}
		
		auto id_inst = get_id_inst(dev_info, &dev_info_data);
		if (id_inst.empty()) {
			continue;
		}

		auto devno = get_devno_from_inst_id(devno_map, id_inst);
		if (!devno) {
			continue;
		}
		
		ret = walker(dev_info, &dev_info_data, devno, ctx);
	}

	return ret;
}

/*
 * A valid USB hardware identifer(stub or vhci accepts) has one of following patterns:
 * - USB\VID_0000&PID_0000&REV_0000
 * - USB\VID_0000&PID_0000 (Is it needed?)
 * Hardware ids of multi-interface device are not valid such as USB\VID_0000&PID_0000&MI_00.
 */
bool is_valid_usb_dev(const std::string &id_hw)
{
	if (id_hw.empty()) {
		return false;
	}

	auto s = id_hw.c_str();

	if (strncmp(s, "USB\\", 4))
		return false;

	if (strncmp(s + 4, "VID_", 4))
		return false;

	if (strlen(s + 8) < 4)
		return false;

	if (strncmp(s + 12, "&PID_", 5))
		return false;

	if (strlen(s + 17) < 4)
		return false;

	if (s[21] == '\0')
		return true;

	if (strncmp(s + 21, "&REV_", 5))
		return false;

	if (strlen(s + 26) < 4)
		return false;

	if (s[30] != '\0')
		return false;

	return true;
}

} // namespace


std::string get_id_hw(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_HARDWAREID);
}

std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> get_intf_detail(HDEVINFO dev_info, SP_DEVINFO_DATA *pdev_info_data, const GUID &guid)
{
	std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> dev_interface_detail;

	auto dev_interface_data = std::make_unique_for_overwrite<SP_DEVICE_INTERFACE_DATA>();
	dev_interface_data->cbSize = sizeof(*dev_interface_data);

	if (!SetupDiEnumDeviceInterfaces(dev_info, pdev_info_data, &guid, 0, dev_interface_data.get())) {
		auto err = GetLastError();
		if (err != ERROR_NO_MORE_ITEMS) {
			dbg("SetupDiEnumDeviceInterfaces error %#x", err);
                }
		return dev_interface_detail;
	}

        DWORD len;
        if (!SetupDiGetDeviceInterfaceDetail(dev_info, dev_interface_data.get(), nullptr, 0, &len, nullptr)) {
                auto err = GetLastError();
                if (err != ERROR_INSUFFICIENT_BUFFER) {
                        dbg("SetupDiGetDeviceInterfaceDetail error %#x", err);
			return dev_interface_detail;
		}
        }

	dev_interface_detail.reset(reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(new char[len]), 
		                   [](auto ptr) { delete[] reinterpret_cast<char*>(ptr); });
	
	dev_interface_detail->cbSize = sizeof(*dev_interface_detail);

	if (!SetupDiGetDeviceInterfaceDetail(dev_info, dev_interface_data.get(), dev_interface_detail.get(), len, nullptr, nullptr)) {
		dbg("SetupDiGetDeviceInterfaceDetail error %#x", GetLastError());
		dev_interface_detail.reset();
	}

	return dev_interface_detail;
}

int traverse_usbdevs(walkfunc_t walker, BOOL present_only, void *ctx)
{
	DWORD flags = DIGCF_ALLCLASSES;
 	if (present_only) {
		flags |= DIGCF_PRESENT;
	}
	
	auto dev_info = SetupDiGetClassDevs(nullptr, "USB", nullptr, flags);
	if (dev_info == INVALID_HANDLE_VALUE) {
		dbg("SetupDiGetClassDevs failed: 0x%lx", GetLastError());
		return ERR_GENERAL;
	}

	DeviceInfoListPtr ptr(dev_info, SetupDiDestroyDeviceInfoList);
	return traverse_dev_info(ptr.get(), walker, ctx);
}

int traverse_intfdevs(walkfunc_t walker, const GUID &guid, void *ctx)
{
	auto dev_info = SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		dbg("SetupDiGetClassDevs failed: 0x%lx", GetLastError());
		return ERR_GENERAL;
	}

	DeviceInfoListPtr ptr(dev_info, SetupDiDestroyDeviceInfoList);
	return traverse_dev_info(dev_info, walker, ctx);
}

bool get_usbdev_info(const std::string &id_hw, unsigned short &vendor, unsigned short &product)
{
	if (!is_valid_usb_dev(id_hw)) {
		return false;
	}

	std::string s = id_hw;

	s[12] = '\0';
	sscanf_s(s.c_str() + 8, "%hx", &vendor);

	s[21] = '\0';
	sscanf_s(s.c_str() + 17, "%hx", &product);

	return true;
}
