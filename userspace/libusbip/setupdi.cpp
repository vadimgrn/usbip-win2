#include "setupdi.h"
#include "common.h"

namespace
{

int traverse_dev_info(HDEVINFO dev_info, const usbip::walkfunc_t &walker)
{
	SP_DEVINFO_DATA	dev_info_data{ sizeof(dev_info_data) };
	int ret = 0;

	for (DWORD i = 0; !ret; ++i) {
		if (SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data)) {
			ret = walker(dev_info, &dev_info_data);
		} else {
			auto err = GetLastError();
			if (err != ERROR_NO_MORE_ITEMS) {
				dbg("SetupDiEnumDeviceInfo error %#lx", err);
			}
			break;
		}
	}

	return ret;
}

} // namespace


std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> usbip::get_intf_detail(
	HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data, const GUID &guid)
{
	std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> dev_interface_detail;

	auto dev_interface_data = std::make_unique_for_overwrite<SP_DEVICE_INTERFACE_DATA>();
	dev_interface_data->cbSize = sizeof(*dev_interface_data);

	if (!SetupDiEnumDeviceInterfaces(dev_info, dev_info_data, &guid, 0, dev_interface_data.get())) {
		auto err = GetLastError();
		if (err != ERROR_NO_MORE_ITEMS) {
			dbg("SetupDiEnumDeviceInterfaces error %#lx", err);
                }
		return dev_interface_detail;
	}

        DWORD len;
        if (!SetupDiGetDeviceInterfaceDetail(dev_info, dev_interface_data.get(), nullptr, 0, &len, nullptr)) {
                auto err = GetLastError();
                if (err != ERROR_INSUFFICIENT_BUFFER) {
                        dbg("SetupDiGetDeviceInterfaceDetail error %#lx", err);
			return dev_interface_detail;
		}
        }

	dev_interface_detail.reset(reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(new char[len]), 
		                   [](auto ptr) { delete[] reinterpret_cast<char*>(ptr); });
	
	dev_interface_detail->cbSize = sizeof(*dev_interface_detail);

	if (!SetupDiGetDeviceInterfaceDetail(dev_info, dev_interface_data.get(), dev_interface_detail.get(), len, nullptr, nullptr)) {
		dbg("SetupDiGetDeviceInterfaceDetail error %#lx", GetLastError());
		dev_interface_detail.reset();
	}

	return dev_interface_detail;
}

int usbip::traverse_intfdevs(const GUID &guid, const walkfunc_t &walker)
{
	if (auto h = GetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)) {
		return traverse_dev_info(h.get(), walker);
	} else {
		dbg("SetupDiGetClassDevs error %#lx", GetLastError());
		return ERR_GENERAL;
	}
}
