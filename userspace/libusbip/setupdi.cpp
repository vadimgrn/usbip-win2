#include "setupdi.h"
#include "common.h"

#include <cstdlib>

namespace
{

auto GetDeviceInstanceId(HDEVINFO devinfo, SP_DEVINFO_DATA *devinfo_data)
{
	std::string id;
	DWORD length;

	SetLastError(ERROR_SUCCESS);
	SetupDiGetDeviceInstanceId(devinfo, devinfo_data, nullptr, 0, &length);

	auto err = GetLastError();
	if (err != ERROR_INSUFFICIENT_BUFFER) {
		dbg("SetupDiGetDeviceInstanceId error %#lx", err);
		return id;
	}

	auto buf = std::make_unique_for_overwrite<char[]>(length);

	if (!SetupDiGetDeviceInstanceId(devinfo, devinfo_data, buf.get(), length, nullptr)) {
		dbg("SetupDiGetDeviceInstanceId error %#lx", GetLastError());
		return id;
	}

	id.assign(buf.get(), length);
	return id;
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

int traverse_dev_info(HDEVINFO dev_info, usbip::walkfunc_t walker, void *ctx)
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
		
		auto id_inst = GetDeviceInstanceId(dev_info, &dev_info_data);
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

} // namespace


std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> usbip::get_intf_detail(
	HDEVINFO dev_info, SP_DEVINFO_DATA *pdev_info_data, const GUID &guid)
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

int usbip::traverse_intfdevs(walkfunc_t walker, const GUID &guid, void *ctx)
{
	if (auto h = GetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)) {
		return traverse_dev_info(h.get(), walker, ctx);
	} else {
		dbg("SetupDiGetClassDevs failed: 0x%lx", GetLastError());
		return ERR_GENERAL;
	}
}
