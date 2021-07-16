#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbdlib.h>
#include <wdfusb.h>
#include <initguid.h>

#include <ude/1.0/UdeCx.h>

#include "vhci_dev.h"
#include "vhci_dbg.h"
#include "vhci_trace.h"

EXTERN_C_START

#define INITABLE __declspec(code_seg("INIT"))
#define PAGEABLE __declspec(code_seg("PAGE"))

#define VHCI_POOLTAG	'ichv'

EXTERN_C_END
