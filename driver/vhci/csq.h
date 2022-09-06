/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip\proto.h>
#include <libdrv\pageable.h>

#include <wdm.h>
#include <usb.h>

struct vpdo_dev_t;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS init_queue(_Inout_ vpdo_dev_t &vpdo);

_IRQL_requires_max_(DISPATCH_LEVEL)
void enqueue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ IRP *irp);

_IRQL_requires_max_(DISPATCH_LEVEL)
IRP *dequeue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ seqnum_t seqnum);

_IRQL_requires_max_(DISPATCH_LEVEL)
IRP *dequeue_irp(_Inout_ vpdo_dev_t &vpdo, _In_ USBD_PIPE_HANDLE handle);

_IRQL_requires_max_(DISPATCH_LEVEL)
IRP *dequeue_irp(_Inout_ vpdo_dev_t &vpdo);
