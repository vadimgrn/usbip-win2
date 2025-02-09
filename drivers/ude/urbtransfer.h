/*
 * Copyright (C) 2021 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <cstddef>
#include <wdm.h>
#include <usb.h>

namespace usbip
{

struct UrbTransfer
{
	using type = _URB_CONTROL_TRANSFER;

	UCHAR Reserved[offsetof(type, PipeHandle)];
	decltype(type::PipeHandle) PipeHandle;
	decltype(type::TransferFlags) TransferFlags;
	decltype(type::TransferBufferLength) TransferBufferLength;
	decltype(type::TransferBuffer) TransferBuffer;
	decltype(type::TransferBufferMDL) TransferBufferMDL;
};

constexpr auto has_transfer_buffer(_In_ const URB &urb)
{
        switch (urb.UrbHeader.Function) {
        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL:

        case URB_FUNCTION_ISOCH_TRANSFER:
        case URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL:

        case URB_FUNCTION_CONTROL_TRANSFER_EX:
        case URB_FUNCTION_CONTROL_TRANSFER:

        case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:

        case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:

        case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
        case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
        case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
        case URB_FUNCTION_GET_STATUS_FROM_OTHER:

        case URB_FUNCTION_VENDOR_DEVICE:
        case URB_FUNCTION_VENDOR_INTERFACE:
        case URB_FUNCTION_VENDOR_ENDPOINT:
        case URB_FUNCTION_VENDOR_OTHER:

        case URB_FUNCTION_CLASS_DEVICE:
        case URB_FUNCTION_CLASS_INTERFACE:
        case URB_FUNCTION_CLASS_ENDPOINT:
        case URB_FUNCTION_CLASS_OTHER:

        case URB_FUNCTION_GET_CONFIGURATION:
        case URB_FUNCTION_GET_INTERFACE:

        case URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR:
                return true;
        }

        return false;
}

inline auto& AsUrbTransfer(_In_ URB &urb) 
{ 
	NT_ASSERT(has_transfer_buffer(urb));
	return reinterpret_cast<UrbTransfer&>(urb); 
}

inline auto& AsUrbTransfer(_In_ const URB &urb) 
{ 
	NT_ASSERT(has_transfer_buffer(urb));
	return reinterpret_cast<const UrbTransfer&>(urb); 
}

} // namespace usbip
