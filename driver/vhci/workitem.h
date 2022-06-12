/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>

extern LOOKASIDE_LIST_EX workitem_list;
NTSTATUS init_workitem_list();

_IO_WORKITEM *alloc_workitem(_In_ void *IoObject);
void free(_In_ _IO_WORKITEM *ctx);