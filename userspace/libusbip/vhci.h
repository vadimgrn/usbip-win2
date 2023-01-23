#pragma once

#include "win_handle.h"

#include <usbip\vhci.h>

#include <string>
#include <vector>

namespace usbip::vhci
{

std::wstring get_path();
libusbip::Handle open(const std::wstring &path = get_path());

std::vector<ioctl_imported_dev> get_imported_devs(HANDLE dev, bool &result);

bool attach(HANDLE dev, ioctl_plugin &r);
int detach(HANDLE dev, int port);

} // namespace usbip::vhci
