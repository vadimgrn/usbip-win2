/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\dllspec.h"

#include <cstdint>
#include <string>

#include <windows.h>

namespace win
{

class USBIP_API Resource
{
public:
	Resource(_In_opt_ HMODULE hModule, _In_ LPCTSTR name, _In_ LPCTSTR type);
	~Resource();

	Resource(const Resource&) = delete;
	Resource& operator =(const Resource&) = delete;

	Resource(Resource&& obj) noexcept : m_impl(obj.release()) {}
	Resource& operator =(Resource&& obj) noexcept;

	explicit operator bool() const noexcept;
	auto operator!() const noexcept { return !bool(*this); } 

	DWORD load(_In_opt_ HMODULE hModule, _In_ LPCTSTR name, _In_ LPCTSTR type);

	void* data() const noexcept;
	DWORD size(HMODULE hModule) const noexcept;

	std::string_view str() const noexcept;

private:
	class Impl;
	Impl *m_impl{}; // std::unique_ptr is not compatible with __declspec(dllexport) for the class

	Impl *release() {
		auto p = m_impl;
		m_impl = nullptr;
		return p;
	}
};

} // namespace win


namespace usbip
{

class USBIP_API UsbIds
{
public:
	UsbIds(std::string_view content);
	~UsbIds();

	UsbIds(const UsbIds&) = delete;
	UsbIds& operator =(const UsbIds&) = delete;

	UsbIds(UsbIds&& obj) noexcept : m_impl(obj.release()) {}
	UsbIds& operator =(UsbIds&& obj) noexcept;

	explicit operator bool() const noexcept;
	bool operator !() const noexcept;

	void load(std::string_view content);

	std::pair<std::string_view, std::string_view> find_product(uint16_t vid, uint16_t pid) const noexcept;

	std::tuple<std::string_view, std::string_view, std::string_view> 
		find_class_subclass_proto(uint8_t class_id, uint8_t subclass_id, uint8_t prot_id) const noexcept;
private:
	class Impl;
	Impl *m_impl{}; // std::unique_ptr is not compatible with __declspec(dllexport) for the class

	Impl *release() {
		auto p = m_impl;
		m_impl = nullptr;
		return p;
	}
};

} // namespace usbip
