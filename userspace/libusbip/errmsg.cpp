#include "errmsg.h"
#include <usbip\consts.h>

const char* usbip::op_status_str(int status)
{
	static_assert(sizeof(op_status_t) <= sizeof(status));

	const char* v[] =
	{
		"ST_OK", "ST_NA", "ST_DEV_BUSY", "ST_DEV_ERR", "ST_NODEV", "ST_ERROR"
	};

	return  status >= 0 && status < sizeof(v)/sizeof(*v) ? 
		v[status] : "op_status_str: unexpected status";
}

const char* usbip::errt_str(int err)
{
	static_assert(sizeof(err_t) <= sizeof(err));

	const char* v[] =
	{
                "ERR_NONE", "ERR_GENERAL", "ERR_CONNECT", "ERR_NETWORK", 
		"ERR_VERSION", "ERR_PROTOCOL", "ERR_PORTFULL"
	};

	if (err < 0) {
		err = -err;
	}

	return err < sizeof(v)/sizeof(*v) ? v[err] : "errt_str: unexpected code";
}
