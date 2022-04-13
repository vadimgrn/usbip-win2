#include "dbgcode.h"
#include <cmath>

const char *dbg_opcode_status(int status)
{
	const char* v[] =
	{
		"ST_OK", "ST_NA", "ST_DEV_BUSY", "ST_DEV_ERR", "ST_NODEV", "ST_ERROR"
	};

	return status >= 0 && status < sizeof(v)/sizeof(*v) ? 
		v[status] : "dbg_opcode_status: unexpected status";
}

const char *dbg_errcode(int err)
{
	const char* v[] =
	{
		"ERR_GENERAL", "ERR_INVARG", "ERR_NETWORK", "ERR_VERSION", "ERR_PROTOCOL", 
		"ERR_STATUS", "ERR_EXIST", "ERR_NOTEXIST", "ERR_DRIVER", "ERR_PORTFULL", 
		"ERR_ACCESS", "ERR_CERTIFICATE"
	};

	err = abs(err);
	return err < sizeof(v)/sizeof(*v) ? v[err] : "dbg_errcode: unexpected code";
}
