#pragma once

#include "usb_util.h"

#define CSPKT_DIRECTION(csp)		(csp)->bmRequestType.Dir
#define CSPKT_REQUEST_TYPE(csp)		(csp)->bmRequestType.Type
#define CSPKT_RECIPIENT(csp)		(csp)->bmRequestType.Recipient
#define CSPKT_REQUEST(csp)		(csp)->bRequest
#define CSPKT_DESCRIPTOR_TYPE(csp)	(csp)->wValue.HiByte
#define CSPKT_DESCRIPTOR_INDEX(csp)	(csp)->wValue.LowByte

#define CSPKT_IS_IN(csp)		(CSPKT_DIRECTION(csp) == BMREQUEST_DEVICE_TO_HOST)

const char *dbg_cspkt_reqtype(UCHAR reqtype);
const char *dbg_cspkt_recipient(UCHAR recip);
const char *dbg_cspkt_request(UCHAR req);
const char *dbg_cspkt_desctype(UCHAR desctype);
const char *dbg_ctlsetup_packet(const USB_DEFAULT_PIPE_SETUP_PACKET *csp);
