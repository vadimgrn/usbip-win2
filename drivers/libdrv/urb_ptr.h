/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
	urb_ptr(_In_ USBD_HANDLE handle) : m_handle(handle) { NT_ASSERT(m_handle); }

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

	auto get() const { return m_urb; }
	void release() { m_urb = nullptr; }

private:
	USBD_HANDLE m_handle{};
	URB *m_urb{};
};

} // namespace libdrv

