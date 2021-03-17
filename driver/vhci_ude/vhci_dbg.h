#pragma once

#include "dbgcommon.h"

#include "vhci_dev.h"
#include "vhci_urbr.h"
#include "dbgcode.h"

extern char	buf_dbg_vhci_ioctl_code[];
extern char	buf_dbg_urbfunc[];
extern char	buf_dbg_setup_packet[];
extern char	buf_dbg_urbr[];

extern int	len_dbg_vhci_ioctl_code;
extern int	len_dbg_urbfunc;
extern int	len_dbg_setup_packet;
extern int	len_dbg_urbr;

extern const char *dbg_vhci_ioctl_code(unsigned int ioctl_code);
extern const char *dbg_urbfunc(USHORT urbfunc);
extern const char *dbg_usb_setup_packet(PCUCHAR packet);
extern const char *dbg_urbr(purb_req_t urbr);

#ifndef DBG
const char *dbg_usbd_status(USBD_STATUS status);
#endif
