#pragma once

#include <wdm.h>
#include <usb.h>

struct vpdo_dev_t;

_IRQL_requires_max_(DISPATCH_LEVEL)
void send_cmd_unlink(_In_ vpdo_dev_t &vpdo, _In_ IRP *irp);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS clear_endpoint_stall(_In_ vpdo_dev_t &vpdo, _In_ USBD_PIPE_HANDLE PipeHandle, _In_opt_ IRP *irp);

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_INTERNAL_DEVICE_CONTROL)
extern "C" NTSTATUS vhci_internal_ioctl(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);
