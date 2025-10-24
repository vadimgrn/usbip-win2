/*
 * Copyright (c) 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <sal.h>

namespace wdm
{

class object_reference
{
public:
	constexpr object_reference() = default;

	object_reference(_In_ void *obj, _In_ bool add_ref = true) :
		object_reference(obj, false, add_ref) {}

	~object_reference();

	object_reference(_In_ const object_reference &other) :
		object_reference(other.m_obj, other.m_defer_delete, true) {}

	object_reference& operator =(_In_ const object_reference &other);

	object_reference(_Inout_ object_reference&& other);
	object_reference& operator =(_Inout_ object_reference&& other);

	constexpr explicit operator bool() const { return m_obj; }
	constexpr bool operator !() const { return !m_obj; }

	template<typename T = void>
	constexpr auto get() const { return static_cast<T*>(m_obj); }

	constexpr void set_defer_delete() { m_defer_delete = true; }
	constexpr auto get_defer_delete() const { return m_defer_delete; }

	void reset(_In_ void *obj = nullptr, _In_ bool add_ref = true) { reset(obj, false, add_ref); }
	void *release();

	void swap(_Inout_ object_reference &other);

private:
	void *m_obj{};
	bool m_defer_delete{};

	object_reference(_In_ void *obj, _In_ bool defer_delete, _In_ bool add_ref);
	void reset(_In_ void *obj, _In_ bool defer_delete, _In_ bool add_ref);
};

inline void swap(_Inout_ object_reference &a, _Inout_ object_reference &b)
{
	a.swap(b);
}

} // namespace wdm

