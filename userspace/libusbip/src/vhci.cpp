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

auto init(_Out_ vhci::ioctl::plugin_hardware &r, _In_ const device_location &loc)
{
        struct {
                char *dst;
                size_t len;
                const std::string &src;
        } const v[] = {
                { r.busid, ARRAYSIZE(r.busid), loc.busid },
                { r.service, ARRAYSIZE(r.service), loc.service },
                { r.host, ARRAYSIZE(r.host), loc.hostname },
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
                        // imported_device_location
                        .port = s.port,
                        // imported_device_properties
                        .devid = s.devid,
                        .speed = win_speed(s.speed),
                        .vendor = s.vendor,
                        .product = s.product,
                };

                {       // imported_device_location
                        auto &loc = d.location;

                        loc.hostname = s.host;
                        assert(loc.hostname.size() < ARRAYSIZE(s.host));

                        loc.service = s.service;
                        assert(loc.service.size() < ARRAYSIZE(s.service));

                        loc.busid = s.busid;
                        assert(loc.busid.size() < ARRAYSIZE(s.busid));
                }
                
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
        success = false;
        std::vector<usbip::imported_device> result;

        constexpr auto devices_offset = offsetof(ioctl::get_imported_devices, devices);

        ioctl::get_imported_devices *r{};
        std::vector<char> buf;

        for (auto cnt = 4; true; cnt <<= 1) {
                buf.resize(ioctl::get_imported_devices_size(cnt));

                r = reinterpret_cast<ioctl::get_imported_devices*>(buf.data());
                r->size = sizeof(*r);

                if (DWORD BytesReturned; // must be set if the last arg is NULL
                    DeviceIoControl(dev, ioctl::GET_IMPORTED_DEVICES, r, sizeof(r->size), 
                                    buf.data(), DWORD(buf.size()), &BytesReturned, nullptr)) {

                        if (BytesReturned < devices_offset) [[unlikely]] {
                                SetLastError(ERROR_USBIP_DRIVER_RESPONSE);
                                return result;
                        }
                                
                        buf.resize(BytesReturned);
                        break;

                } else if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                        return result;
                }
        }

        auto devices_size = buf.size() - devices_offset;
        success = !(devices_size % sizeof(*r->devices));

        if (!success) {
                libusbip::output("{}: N*sizeof(imported_device) != {}", __func__, devices_size);
                SetLastError(ERROR_USBIP_DRIVER_RESPONSE);
        } else if (auto cnt = devices_size/sizeof(*r->devices)) {
                assign(result, r->devices, cnt);
        }

        return result;
}

int usbip::vhci::attach(_In_ HANDLE dev, _In_ const device_location &location)
{
        ioctl::plugin_hardware r {{ .size = sizeof(r) }};
        if (!init(r, location)) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return 0;
        }

        constexpr auto outlen = offsetof(ioctl::plugin_hardware, port) + sizeof(r.port);

        if (DWORD BytesReturned; // must be set if the last arg is NULL
            DeviceIoControl(dev, ioctl::PLUGIN_HARDWARE, &r, sizeof(r), &r, outlen, &BytesReturned, nullptr)) {

                if (BytesReturned != outlen) [[unlikely]] {
                        SetLastError(ERROR_USBIP_DRIVER_RESPONSE);
                } else {
                        assert(r.port > 0);
                        return r.port;
                }
        }

        return 0;
}

bool usbip::vhci::detach(_In_ HANDLE dev, _In_ int port)
{
        ioctl::plugout_hardware r { .port = port };
        r.size = sizeof(r);

        DWORD BytesReturned; // must be set if the last arg is NULL
        return DeviceIoControl(dev, ioctl::PLUGOUT_HARDWARE, &r, sizeof(r), nullptr, 0, &BytesReturned, nullptr);
}
