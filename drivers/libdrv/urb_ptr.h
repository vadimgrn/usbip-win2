/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wdm.h>
#include <usb.h>
#include <usbdlib.h>

namespace libdrv
{

class urb_ptr
{
public:
	explicit urb_ptr(_In_ USBD_HANDLE handle) : m_handle(handle) { NT_ASSERT(m_handle); }

	urb_ptr(_In_ USBD_HANDLE handle, _In_ URB *urb) : m_handle(handle), m_urb(urb) { NT_ASSERT(m_handle); }

	~urb_ptr()
	{
		if (m_urb) {
			USBD_UrbFree(m_handle, m_urb);
		}
	}

	auto alloc(_In_ IO_STACK_LOCATION *stack)
	{
		auto st = m_urb ? STATUS_ALREADY_INITIALIZED : USBD_UrbAllocate(m_handle, &m_urb); 
		if (NT_SUCCESS(st)) {
			USBD_AssignUrbToIoStackLocation(m_handle, stack, m_urb);
		}
		return st;
	}

	auto handle() const { return m_handle; }
	auto get() const { return m_urb; }

	auto release() 
	{
		auto urb = m_urb;
		m_urb = nullptr; 
		return urb;
	}

private:
	USBD_HANDLE m_handle{};
	URB *m_urb{};
};

} // namespace libdrv

