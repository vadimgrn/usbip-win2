#pragma once

#include "pageable.h"
#include <ntdef.h>

struct vpdo_dev_t;
PAGEABLE NTSTATUS init_queues(vpdo_dev_t &vpdo);

constexpr void *InsertTail() { return nullptr; }
constexpr void *InsertHead() { return InsertTail; }

// for read irp only
constexpr void *InsertTailIfRxEmpty() { return init_queues; }
