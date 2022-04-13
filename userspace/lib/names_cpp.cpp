#include "names_cpp.h"
#include "usbip_util.h"
#include "names.h"

InitUsbNames::InitUsbNames()
{
        auto path = get_module_dir() + "usb.ids";
        m_ok = !names_init(path.c_str());
}

InitUsbNames::~InitUsbNames()
{
        if (m_ok) {
                names_free();
        }
}
