/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "utils.h"

#include <wdm.h>
#include <usb.h>

extern "C" {
#include <usbdlib.h>
}

namespace libdrv
{

class urb_ptr
{
public:
	constexpr urb_ptr() = default;

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr explicit urb_ptr(_In_ USBD_HANDLE handle) :
		urb_ptr(handle, nullptr) {}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr urb_ptr(_In_ USBD_HANDLE handle, _In_opt_ URB *urb) :
		m_handle(handle), m_urb(urb) 
	{
		NT_ASSERT(m_handle);
	}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	~urb_ptr();

	urb_ptr(const urb_ptr&) = delete;
	urb_ptr& operator =(const urb_ptr&) = delete;

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr urb_ptr(urb_ptr &&other) :
		m_handle(other.m_handle),
		m_urb(other.release())
	{
		other.m_handle = nullptr;
	}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr urb_ptr& operator =(urb_ptr &&other)
	{
		urb_ptr(static_cast<urb_ptr&&>(other)).swap(*this);
		return *this;
	}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	NTSTATUS alloc(_In_opt_ IO_STACK_LOCATION *stack = nullptr);

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void reset(_In_opt_ URB *urb = nullptr)
	{
		if (m_urb != urb) {
			urb_ptr(m_handle, urb).swap(*this);
		}
	}

	constexpr explicit operator bool() const { return m_urb != nullptr; }
	constexpr bool operator !() const { return !m_urb; }

	constexpr bool operator ==(decltype(nullptr)) const { return m_urb == nullptr; }
	constexpr bool operator !=(decltype(nullptr)) const { return m_urb != nullptr; }

	constexpr auto handle() const { return m_handle; }
	constexpr auto get() const { return m_urb; }

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr auto operator ->() const
	{
		NT_ASSERT(m_urb);
		return m_urb;
	}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr auto& operator *() const
	{
		NT_ASSERT(m_urb);
		return *m_urb;
	}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr URB* release()
	{
		auto urb = m_urb;
		m_urb = nullptr;
		return urb;
	}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	constexpr void swap(_Inout_ urb_ptr &other)
	{
		::swap(m_handle, other.m_handle);
		::swap(m_urb, other.m_urb);
	}

private:
	USBD_HANDLE m_handle{};
	URB *m_urb{};
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr void swap(_Inout_ urb_ptr &a, _Inout_ urb_ptr &b)
{
	a.swap(b);
}

} // namespace libdrv

