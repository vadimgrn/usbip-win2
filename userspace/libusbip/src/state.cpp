/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "state.h"
#include "output.h"
#include "..\vhci.h"
#include "..\format_message.h"

namespace
{

} // namespace


bool usbip::save_imported_devices(_In_ const std::vector<imported_device>&)
{
        return false;
}

auto usbip::load_imported_devices(_Out_ bool &success) -> std::vector<device_location>
{
        success = false;
        std::vector<device_location> devs;
        return devs;
}
