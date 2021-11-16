#include "dbgcode.h"
#include "strutil.h"

#include <ntstrsafe.h>

enum { NAMECODE_BUF_MAX = 256 };

static const namecode_t namecodes_usbd_status[] = 
{
	K_V(USBD_STATUS_SUCCESS)
	K_V(USBD_STATUS_PENDING)
	K_V(USBD_STATUS_STALL_PID)
	{0,0}
};

const char *dbg_namecode_buf(
	const namecode_t *namecodes, const char *codetype, unsigned int code, 
	char *buf, unsigned int buf_max, unsigned int *written)
{
	unsigned int cnt = 0;

	for ( ; namecodes->name; ++namecodes) {
		if (code == namecodes->code) {
			cnt += libdrv_snprintf(buf, buf_max , "%s", namecodes->name);
			break;
		}
	}

	if (!cnt) {
		cnt += libdrv_snprintf(buf, buf_max, "Unknown %s code: %x", codetype, code);
	}

	if (written) {
		*written = cnt + 1; // '\0'
	}
	
	return buf;
}

const char *dbg_namecode(const namecode_t *namecodes, const char *codetype, unsigned int code)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes, codetype, code, buf, sizeof(buf), NULL);
}

const char *dbg_usbd_status(USBD_STATUS status)
{
	static char buf[NAMECODE_BUF_MAX];
	return dbg_namecode_buf(namecodes_usbd_status, "usbd status", status, buf, sizeof(buf), NULL);
}
