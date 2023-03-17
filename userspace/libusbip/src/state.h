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
 * @return status of the operation
 */
USBIP_API bool save_imported_devices(_In_ const std::vector<imported_device> &devices);

/*
 * @param success status of the operation
 * @return imported devices stashed by save_imported_devices()
 */
USBIP_API std::vector<device_location> load_imported_devices(_Out_ bool &success);

} // namespace usbip
