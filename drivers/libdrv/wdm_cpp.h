/*
 * Copyright (c) 2025-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <sal.h>
#include <kernelspecs.h>

namespace wdm
{

class object_reference
{
public:
	constexpr object_reference() = default;

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	object_reference(_In_opt_ void *obj, _In_ bool add_ref = true) :
		object_reference(obj, false, add_ref) {}

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	object_reference(_In_opt_ void *obj, _In_ bool defer_delete, _In_ bool add_ref);

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	~object_reference();

	object_reference(_In_ const object_reference &other) :
		object_reference(other.m_obj, other.m_defer_delete, true) {}

	object_reference& operator =(_In_ const object_reference &other);

	object_reference(_Inout_ object_reference&& other);
	object_reference& operator =(_Inout_ object_reference&& other);

	constexpr explicit operator bool() const { return m_obj; }
	constexpr bool operator !() const { return !m_obj; }

	constexpr bool operator ==(decltype(nullptr)) const { return m_obj == nullptr; }

	friend constexpr bool operator ==(const object_reference &a, const object_reference &b) {
		return a.m_obj == b.m_obj;
	}

	template<typename T = void>
	constexpr auto get(this auto&& self) { return static_cast<T*>(self.m_obj); }

	constexpr void set_defer_delete() { m_defer_delete = true; }
	constexpr auto get_defer_delete() const { return m_defer_delete; }

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void reset(_In_opt_ void *obj = nullptr, _In_ bool add_ref = true) { reset(obj, false, add_ref); }

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void reset(_In_opt_ void *obj, _In_ bool defer_delete, _In_ bool add_ref);

	void *release();

	_IRQL_requires_same_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void swap(_Inout_ object_reference &other);

private:
	void *m_obj{};
	bool m_defer_delete{};
};

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void swap(_Inout_ object_reference &a, _Inout_ object_reference &b)
{
	a.swap(b);
}

template<typename T>
class object_ref : public object_reference
{
public:
	using object_reference::object_reference;

	constexpr explicit object_ref(T *obj, bool add_ref = true) :
		object_reference(obj, add_ref) {}

	constexpr explicit object_ref(T *obj, bool defer_delete, bool add_ref) :
		object_reference(obj, defer_delete, add_ref) {}

	constexpr T* get(this auto&& self) { return self.object_reference::template get<T>(); }
	constexpr T* operator ->(this auto&& self) { auto p = self.get(); NT_ASSERT(p); return p; }
	constexpr T& operator *(this auto&& self) { auto p = self.get(); NT_ASSERT(p); return *p; }

	T* release() { return static_cast<T*>(object_reference::release()); }
};

} // namespace wdm

