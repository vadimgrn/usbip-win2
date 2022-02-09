#pragma once

#include "pageable.h"
#include <ntdef.h>

struct vpdo_dev_t;
PAGEABLE NTSTATUS init_queues(vpdo_dev_t &vpdo);

// InsertContext flags for IoCsqInsertIrpEx, bits
enum { 
	CSQ_FAIL_IF_URB_PENDING = 1, // IN, for read irp
	// below for urb irp
	CSQ_INSERT_HEAD = 0, // IN
	CSQ_INSERT_TAIL, // IN
	CSQ_READ_PENDING // OUT
};
