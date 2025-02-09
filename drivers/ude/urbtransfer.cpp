/*
 * Copyright (C) 2021 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "urbtransfer.h"

using usbip::UrbTransfer;

/*
 * These URBs have the same layout from the beginning of the structure.
 */
const auto off_pipe = offsetof(UrbTransfer::type, PipeHandle);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, PipeHandle) == off_pipe);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, PipeHandle) == off_pipe);
static_assert(offsetof(_URB_ISOCH_TRANSFER, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, PipeHandle) == off_pipe);
//static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, PipeHandle) == off_pipe);

const auto off_flags = offsetof(UrbTransfer::type, TransferFlags);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, TransferFlags) == off_flags);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, TransferFlags) == off_flags);
static_assert(offsetof(_URB_ISOCH_TRANSFER, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, TransferFlags) == off_flags);
static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferFlags) == off_flags);
//static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferFlags) == off_flags);

const auto off_len = offsetof(UrbTransfer::type, TransferBufferLength);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_ISOCH_TRANSFER, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBufferLength) == off_len);
static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBufferLength) == off_len);

const auto off_buf = offsetof(UrbTransfer::type, TransferBuffer);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_ISOCH_TRANSFER, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBuffer) == off_buf);
static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBuffer) == off_buf);

const auto off_mdl = offsetof(UrbTransfer::type, TransferBufferMDL);
static_assert(offsetof(_URB_CONTROL_TRANSFER_EX, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_BULK_OR_INTERRUPT_TRANSFER, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_ISOCH_TRANSFER, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_DESCRIPTOR_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_GET_STATUS_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_VENDOR_OR_CLASS_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_GET_INTERFACE_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_CONTROL_GET_CONFIGURATION_REQUEST, TransferBufferMDL) == off_mdl);
static_assert(offsetof(_URB_OS_FEATURE_DESCRIPTOR_REQUEST, TransferBufferMDL) == off_mdl);
