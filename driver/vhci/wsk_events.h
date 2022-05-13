#pragma once

#include "usbip_proto.h"
#include <wdm.h>

struct vpdo_dev_t;
struct _WSK_DATA_INDICATION;

NTSTATUS WskReceiveEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags, _In_opt_ _WSK_DATA_INDICATION *DataIndication,
        _In_ SIZE_T BytesIndicated, _Inout_ SIZE_T *BytesAccepted);

NTSTATUS WskDisconnectEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags);

IRP *dequeue_irp(vpdo_dev_t &vpdo, seqnum_t seqnum);