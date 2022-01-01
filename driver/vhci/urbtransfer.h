#pragma once

#include <ntdef.h>
#include <usb.h>

struct UrbTransfer
{
	UCHAR Reserved[offsetof(_URB_CONTROL_TRANSFER, PipeHandle)];
	USBD_PIPE_HANDLE PipeHandle;
	ULONG TransferFlags;
	ULONG TransferBufferLength;
	PVOID TransferBuffer;
	PMDL TransferBufferMDL;
};

static_assert(offsetof(_URB_CONTROL_TRANSFER, PipeHandle) == offsetof(UrbTransfer, PipeHandle));
static_assert(sizeof(UrbTransfer::PipeHandle) == sizeof(_URB_CONTROL_TRANSFER::PipeHandle));
static_assert(sizeof(UrbTransfer::TransferFlags) == sizeof(_URB_CONTROL_TRANSFER::TransferFlags));
static_assert(sizeof(UrbTransfer::TransferBufferLength) == sizeof(_URB_CONTROL_TRANSFER::TransferBufferLength));
static_assert(sizeof(UrbTransfer::TransferBuffer) == sizeof(_URB_CONTROL_TRANSFER::TransferBuffer));
static_assert(sizeof(UrbTransfer::TransferBufferMDL) == sizeof(_URB_CONTROL_TRANSFER::TransferBufferMDL));

inline auto AsUrbTransfer(URB *urb) { return reinterpret_cast<UrbTransfer*>(urb); }
inline auto AsUrbTransfer(const URB *urb) { return reinterpret_cast<const UrbTransfer*>(urb); }

/*
* These URBs have the same layout from the beginning of the structure.
*/
const auto off_pipe = offsetof(_URB_CONTROL_TRANSFER, PipeHandle);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, PipeHandle) == off_pipe);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, PipeHandle) == off_pipe);
static_assert(offsetof(_URB_ISOCH_TRANSFER, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, PipeHandle) == off_pipe);

const auto off_flags = offsetof(_URB_CONTROL_TRANSFER, TransferFlags);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, TransferFlags) == off_flags);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, TransferFlags) == off_flags);
static_assert(offsetof(_URB_ISOCH_TRANSFER, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, TransferFlags) == off_flags);
static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferFlags) == off_flags);

const auto off_len = offsetof(_URB_CONTROL_TRANSFER, TransferBufferLength);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_ISOCH_TRANSFER, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBufferLength) == off_len);

const auto off_buf = offsetof(_URB_CONTROL_TRANSFER, TransferBuffer);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_ISOCH_TRANSFER, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBuffer) == off_buf);

const auto off_mdl = offsetof(_URB_CONTROL_TRANSFER, TransferBufferMDL);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_ISOCH_TRANSFER, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBufferMDL) == off_mdl);

