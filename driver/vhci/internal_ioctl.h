#pragma once

#include <wdm.h>

struct vpdo_dev_t;

NTSTATUS send_to_server(vpdo_dev_t*, IRP*);
NTSTATUS send_cmd_unlink(vpdo_dev_t*, IRP*);

struct _WSK_DATA_INDICATION;

NTSTATUS WskReceiveEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags, _In_opt_ _WSK_DATA_INDICATION *DataIndication,
        _In_ SIZE_T BytesIndicated, _Inout_ SIZE_T *BytesAccepted);

NTSTATUS WskDisconnectEvent(_In_opt_ PVOID SocketContext, _In_ ULONG Flags);