/*
 * Copyright (C) 2022 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "codeseg.h"

namespace libdrv
{

class RaiseIrql
{
public:
	explicit RaiseIrql(_In_ KIRQL new_irql) { KeRaiseIrql(new_irql, &old_irql); }
	~RaiseIrql() { KeLowerIrql(old_irql); }

	RaiseIrql(_In_ const RaiseIrql&) = delete;
	RaiseIrql& operator =(_In_ const RaiseIrql&) = delete;

private:
	KIRQL old_irql{};
};

class irp_ptr
{
public:
	template<typename ...Args>
	irp_ptr(Args&&... args) : m_irp(IoAllocateIrp(args...)) {}

	~irp_ptr()
	{
		if (m_irp) {
			IoFreeIrp(m_irp);
		}
	}

	explicit operator bool() const { return m_irp; }
	auto operator !() const { return !m_irp; }

	auto get() const { return m_irp; }
	
	auto release() 
	{ 
		auto irp = m_irp;
		m_irp = nullptr; 
		return irp;
	}

private:
	IRP *m_irp{};
};


inline auto list_entry(_In_ IRP *irp)
{
	return &irp->Tail.Overlay.ListEntry;
}

inline auto get_irp(_In_ LIST_ENTRY *entry)
{
	return CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
}

template<auto N>
inline decltype(auto) argv(_In_ IRP *irp)
{
	NT_ASSERT(irp);
        return irp->Tail.Overlay.DriverContext[N];
}

template<typename R, auto N>
inline auto argv(_In_ IRP *irp)
{
	return static_cast<R*>(argv<N>(irp));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS ForwardIrp(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS ForwardIrpSynchronously(_In_ DEVICE_OBJECT *devobj, _In_ IRP *irp);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS CompleteRequest(_In_ IRP *irp, _In_ NTSTATUS status);

inline void CompleteRequest(_In_ IRP *irp)
{
        IoCompleteRequest(irp, IO_NO_INCREMENT);
}

} // namespace libdrv
