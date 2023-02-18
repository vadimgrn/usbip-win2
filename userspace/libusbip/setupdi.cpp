#include "setupdi.h"
#include "output.h"
#include "last_error.h"

namespace
{

using namespace usbip;

auto traverse_dev_info(HDEVINFO dev_info, const walkfunc_t &walker)
{
	SP_DEVINFO_DATA	dev_info_data{ .cbSize = sizeof(dev_info_data) };

	for (DWORD i = 0; ; ++i) {
		if (!SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data)) {
			if (set_last_error err; err.get() != ERROR_NO_MORE_ITEMS) {
				libusbip::output("SetupDiEnumDeviceInfo error {:#x}", err.get());
			}
			return false;
		} else if (walker(dev_info, &dev_info_data)) {
			return true;
		}
	}
}

} // namespace


/*
 * Call GetLastError() if nullptr is returned.
 * OK if GetLastError() returns ERROR_NO_MORE_ITEMS.
 */
std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> usbip::get_intf_detail(
	HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data, const GUID &guid)
{
	std::shared_ptr<SP_DEVICE_INTERFACE_DETAIL_DATA> dev_interface_detail;

	auto dev_interface_data = std::make_unique_for_overwrite<SP_DEVICE_INTERFACE_DATA>();
	dev_interface_data->cbSize = sizeof(*dev_interface_data);

	if (!SetupDiEnumDeviceInterfaces(dev_info, dev_info_data, &guid, 0, dev_interface_data.get())) {
		if (set_last_error err; err.get() != ERROR_NO_MORE_ITEMS) {
			libusbip::output("SetupDiEnumDeviceInterfaces error {:#x}", err.get());
		}
		return dev_interface_detail;
	}

        DWORD size;
        if (!SetupDiGetDeviceInterfaceDetail(dev_info, dev_interface_data.get(), nullptr, 0, &size, nullptr)) {
		if (set_last_error err; err.get() != ERROR_INSUFFICIENT_BUFFER) {
			libusbip::output("SetupDiGetDeviceInterfaceDetail error {:#x}", err.get());
			return dev_interface_detail;
		}
        }

	dev_interface_detail.reset(reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(new char[size]), 
		                   [] (auto ptr) { delete[] reinterpret_cast<char*>(ptr); });
	
	dev_interface_detail->cbSize = sizeof(*dev_interface_detail);

	if (!SetupDiGetDeviceInterfaceDetail(dev_info, dev_interface_data.get(), dev_interface_detail.get(), size, nullptr, nullptr)) {
		set_last_error err;
		libusbip::output("SetupDiGetDeviceInterfaceDetail error {:#x}", err.get());
		dev_interface_detail.reset();
	}

	return dev_interface_detail;
}

/*
 * Call GetLastError() if false is returned. 
 * Interface not found if GetLastError() returns ERROR_NO_MORE_ITEMS.
 */
bool usbip::traverse_intfdevs(const GUID &guid, const walkfunc_t &walker)
{
	auto ok = false;
	
	if (auto h = hdevinfo(SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE))) {
		ok = traverse_dev_info(h.get(), walker);
	} else {
		set_last_error err;
		libusbip::output("SetupDiGetClassDevs error {:#x}", err.get());
	}

	return ok;
}
