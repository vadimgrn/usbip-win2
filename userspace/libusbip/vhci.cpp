#include "vhci.h"
#include "setupdi.h"
#include "log.h"
#include "last_error.h"

#include <resources\messages.h>

#include <initguid.h>
#include <usbip\vhci.h>

namespace
{

auto walker_devpath(std::wstring &path, const GUID &guid, HDEVINFO dev_info, SP_DEVINFO_DATA *data)
{
        if (auto inf = usbip::get_intf_detail(dev_info, data, guid)) {
                assert(inf->cbSize == sizeof(*inf)); // this is not a size/length of DevicePath
                path = inf->DevicePath;
                return true;
        }

        return false;
}

} // namespace


/*
 * @return "" if the driver is not loaded 
 */
std::wstring usbip::vhci::get_path()
{
        std::wstring path;
        auto &guid = vhci::GUID_DEVINTERFACE_USB_HOST_CONTROLLER;

        auto f = [&path, &guid] (auto&&...args) 
        {
                return walker_devpath(path, guid, std::forward<decltype(args)>(args)...); 
        };

        traverse_intfdevs(guid, f);
        return path;
}

/*
 * Call GetLastError if returned handle is invalid.
 */
auto usbip::vhci::open(const std::wstring &path) -> Handle
{
        Handle h;

        if (path.empty()) { // get_path() failed
                SetLastError(ERROR_USBIP_VHCI_NOT_FOUND);
        } else {
                h.reset(CreateFile(path.c_str(),
                        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        }

        return h;
}

/*
 * Call GetLastError if result is false.
 */
auto usbip::vhci::get_imported_devices(HANDLE dev, bool &success) -> std::vector<imported_device>
{
        std::vector<imported_device> v(TOTAL_PORTS);
        auto idevs_bytes = DWORD(v.size()*sizeof(v[0]));

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::get_imported_devices, nullptr, 0, 
                            v.data(), idevs_bytes, &BytesReturned, nullptr)) {
                assert(!(BytesReturned % sizeof(v[0])));
                v.resize(BytesReturned / sizeof(v[0]));
                success = true;
        } else {
                success = false;
                v.clear();
        }

        return v;
}

/*
 * Call std::generic_category().message() if return != 0.
 */
errno_t usbip::vhci::init(
        _Out_ ioctl_plugin_hardware &r, 
        _In_ std::string_view host, 
        _In_ std::string_view service,
        _In_ std::string_view busid)
{
        struct {
                char *dst;
                size_t len;
                const std::string_view &src;
        } const v[] = {
                { r.busid, ARRAYSIZE(r.busid), busid },
                { r.service, ARRAYSIZE(r.service), service },
                { r.host, ARRAYSIZE(r.host), host },
        };

        for (auto &i: v) {
                if (auto err = strncpy_s(i.dst, i.len, i.src.data(), i.src.size())) {
                        libusbip::log->error("strncpy_s('{}') error #{}", i.src, err);
                        return err;
                }
        }

        r.out = decltype(r.out){}; // clear
        return 0;
}

/*
 * @return hub port number, [1..TOTAL_PORTS]. Call GetLastError() if zero is returned. 
 */
int usbip::vhci::attach(_In_ HANDLE dev, _Inout_ ioctl_plugin_hardware &r)
{
        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::plugin_hardware, &r, sizeof(r), &r.out, sizeof(r.out), &BytesReturned, nullptr)) {
                assert(BytesReturned == sizeof(r.out));
                SetLastError(r.out.error);
                return r.out.port;
        }

        return 0;
}

/*
* @return call GetLastError() if false is returned
*/
bool usbip::vhci::detach(HANDLE dev, int port)
{
        ioctl_plugout_hardware r { .port = port };

        DWORD BytesReturned; // must be set if the last arg is NULL
        auto ok = DeviceIoControl(dev, ioctl::plugout_hardware, &r, sizeof(r), nullptr, 0, &BytesReturned, nullptr);
        assert(!BytesReturned);

        return ok;
}
