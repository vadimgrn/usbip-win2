/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\dllspec.h"
#include <vector>

namespace usbip
{

struct device_location;
struct imported_device;

/*
 * @param devices result of vhci::get_imported_devices()
 * @return call GetLastError if false is returned
 */
USBIP_API bool save_imported_devices(_In_ const std::vector<imported_device> &devices);

/*
 * @param success call GetLastError if false is returned
 * @return imported devices stashed by save_imported_devices() if success
 */
USBIP_API std::vector<device_location> load_imported_devices(_Out_ bool &success);

} // namespace usbip
