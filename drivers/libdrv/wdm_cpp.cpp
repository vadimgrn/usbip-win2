/*
 * Copyright (c) 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wdm_cpp.h"
#include "pair.h" // swap

#include <wdm.h>

wdm::object_reference::object_reference(_In_ void *obj, _In_ bool defer_delete, _In_ bool add_ref) : 
	m_obj(obj),
	m_defer_delete(defer_delete)
{
	if (obj && add_ref) {
		ObReferenceObject(obj);
	}
}

wdm::object_reference::~object_reference()
{
	if (!m_obj) {
		//
	} else if (m_defer_delete) {
		ObDereferenceObjectDeferDelete(m_obj);
	} else {
		ObDereferenceObject(m_obj);
	}
}

wdm::object_reference::object_reference(_Inout_ object_reference&& other) :
	m_obj(other.m_obj),
	m_defer_delete(other.m_defer_delete)
{
	other.release();
}

auto wdm::object_reference::operator =(_In_ const object_reference &other) -> object_reference&
{
	reset(other.m_obj, other.m_defer_delete, true);
	return *this;
}

auto wdm::object_reference::operator =(_Inout_ object_reference&& other) -> object_reference&
{
	auto defer_delete = other.m_defer_delete;
	auto obj = other.release();

	reset(obj, defer_delete, false);
	return *this;
}

void wdm::object_reference::reset(_In_ void *obj, _In_ bool defer_delete, _In_ bool add_ref)
{
	if (m_obj != obj) {
		object_reference(obj, defer_delete, add_ref).swap(*this);
	}
}

void* wdm::object_reference::release()
{
	auto obj = m_obj;

	m_obj = nullptr;
	m_defer_delete = false;

	return obj;
}

void wdm::object_reference::swap(_Inout_ object_reference &other)
{
	::swap(m_obj, other.m_obj);
	::swap(m_defer_delete, other.m_defer_delete);
}
