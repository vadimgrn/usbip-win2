/*
 * Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "..\vhci.h"

#include "last_error.h"
#include "device_speed.h"
#include "strconv.h"
#include "output.h"

#include <resources\messages.h>

#include <cfgmgr32.h>

#include <initguid.h>
#include <usbip\vhci.h>

namespace
{

using namespace usbip;

auto init(_Out_ vhci::ioctl::plugin_hardware &r, _In_ const attach_info &info)
{
        struct {
                char *dst;
                size_t len;
                const std::string &src;
        } const v[] = {
                { r.busid, ARRAYSIZE(r.busid), info.busid },
                { r.service, ARRAYSIZE(r.service), info.service },
                { r.host, ARRAYSIZE(r.host), info.hostname },
        };

        for (auto &i: v) {
                if (auto err = strncpy_s(i.dst, i.len, i.src.data(), i.src.size())) {
                        libusbip::output("strncpy_s('{}') error #{} {}", i.src, err, 
                                          std::generic_category().message(err));
                        return false;
                }
        }

        return true;
}

void assign(_Out_ std::vector<imported_device> &dst, _In_ const vhci::imported_device *src, _In_ size_t cnt)
{
        assert(dst.empty());
        dst.reserve(cnt);

        for (size_t i = 0; i < cnt; ++i) {
                auto &s = src[i];

                imported_device d { 
                        { .hostname = s.host,
                          .service = s.service,
                          .busid = s.busid }
                };

                // imported_device_location 
                assert(d.hostname.size() < ARRAYSIZE(s.host));
                assert(d.service.size() < ARRAYSIZE(s.service));
                assert(d.busid.size() < ARRAYSIZE(s.busid));
                d.port = s.port;

                // imported_device_properties
                d.devid = s.devid;
                d.speed = win_speed(s.speed);
                d.vendor = s.vendor;
                d.product = s.product;

                dst.push_back(std::move(d));
        }
}

auto get_path()
{
        auto guid = const_cast<GUID*>(&vhci::GUID_DEVINTERFACE_USB_HOST_CONTROLLER);
        std::wstring path;

        for (std::wstring multisz; true; ) {

                ULONG cch;
                if (auto err = CM_Get_Device_Interface_List_Size(&cch, guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT)) {
                        libusbip::output("CM_Get_Device_Interface_List_Size error #{}", err);
                        auto code = CM_MapCrToWin32Err(err, ERROR_INVALID_PARAMETER);
                        SetLastError(code);
                        return path;
                } 

                multisz.resize(cch); // "path1\0path2\0pathn\0\0"

                switch (auto err = CM_Get_Device_Interface_List(guid, nullptr, multisz.data(), cch, CM_GET_DEVICE_INTERFACE_LIST_PRESENT)) {
                case CR_SUCCESS:
                        if (auto v = split_multisz(multisz); auto n = v.size()) {
                                if (n == 1) {
                                        path = v.front();
                                        assert(!path.empty());
                                } else {
                                        libusbip::output("CM_Get_Device_Interface_List: {} paths returned", n);
                                        for (auto &s: v) {
                                                libusbip::output(s);
                                        }
                                        SetLastError(ERROR_USBIP_DEVICE_INTERFACE_LIST);
                                }
                        } else {
                                assert(multisz.size() == 1); // if not found, returns CR_SUCCESS and ""
                                assert(!multisz.front());
                                SetLastError(ERROR_USBIP_VHCI_NOT_FOUND);
                        }
                        return path;
                case CR_BUFFER_SMALL:
                        break;
                default:
                        libusbip::output("CM_Get_Device_Interface_List error #{}", err);
                        auto code = CM_MapCrToWin32Err(err, ERROR_NOT_ENOUGH_MEMORY);
                        SetLastError(code);
                        return path;
                }
        }
}

} // namespace


int usbip::vhci::get_total_ports() noexcept
{
        return TOTAL_PORTS;
}

auto usbip::vhci::open() -> Handle
{
        Handle h;

        if (auto path = get_path(); !path.empty()) {
                h.reset(CreateFile(path.c_str(), GENERIC_READ | GENERIC_WRITE, 
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, 
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        }

        return h;
}

std::vector<usbip::imported_device> usbip::vhci::get_imported_devices(_In_ HANDLE dev, _Out_ bool &success)
{
        std::vector<usbip::imported_device> result;

        ioctl::get_imported_devices *r{};
        std::vector<char> v(sizeof(*r) + (get_total_ports() - ARRAYSIZE(r->devices))*sizeof(*r->devices));
        r = reinterpret_cast<ioctl::get_imported_devices*>(v.data());
        r->size = sizeof(*r);

        DWORD BytesReturned; // must be set if the last arg is NULL
        success = DeviceIoControl(dev, ioctl::GET_IMPORTED_DEVICES, r, sizeof(r->size), 
                                  v.data(), DWORD(v.size()), &BytesReturned, nullptr);

        if (!success) {
                return result;
        }

        const auto devices_offset = offsetof(ioctl::get_imported_devices, devices);

        assert(BytesReturned >= devices_offset);
        BytesReturned -= devices_offset;

        assert(!(BytesReturned % sizeof(*r->devices)));

        if (auto cnt = BytesReturned/sizeof(*r->devices)) {
                assign(result, r->devices, cnt);
        }

        SetLastError(r->error);
        return result;
}

int usbip::vhci::attach(_In_ HANDLE dev, _In_ const attach_info &info)
{
        ioctl::plugin_hardware r {{ .size = sizeof(r) }};
        if (!init(r, info)) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return 0;
        }

        const auto outlen = offsetof(ioctl::plugin_hardware, port) + sizeof(r.port);

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::PLUGIN_HARDWARE, &r, sizeof(r), &r, outlen, &BytesReturned, nullptr)) {
                assert(BytesReturned == outlen);
                SetLastError(r.error);
                return r.port;
        }

        return 0;
}

bool usbip::vhci::detach(HANDLE dev, int port)
{
        ioctl::plugout_hardware r { .port = port };
        r.size = sizeof(r);

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::PLUGOUT_HARDWARE, &r, sizeof(r), 
                            &r, sizeof(r.error), &BytesReturned, nullptr)) {
                assert(BytesReturned == sizeof(r.error));
                SetLastError(r.error);
                return !r.error;
        }

        return false;
}
