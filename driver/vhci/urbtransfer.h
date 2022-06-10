/*
 * Copyright (C) 2021, 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <stddef.h>
#include <ntdef.h>
#include <usb.h>

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

inline auto AsUrbTransfer(URB *urb) { return reinterpret_cast<UrbTransfer*>(urb); }
inline auto AsUrbTransfer(const URB *urb) { return reinterpret_cast<const UrbTransfer*>(urb); }

bool has_transfer_buffer(_In_ const URB &urb);