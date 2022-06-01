/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <windows.h>

class Resource
{
public:
	Resource(_In_opt_ HMODULE hModule, _In_ LPCSTR name, _In_ LPCSTR type) { load(hModule, name, type); }

	explicit operator bool() const noexcept { return hResInfo && hResData; }
	auto operator!() const noexcept { return !bool(*this); } 

	DWORD load(_In_opt_ HMODULE hModule, _In_ LPCSTR name, _In_ LPCSTR type);

	auto data() const noexcept { return LockResource(hResData); }
	auto size(HMODULE hModule) const noexcept { return SizeofResource(hModule, hResInfo); }

	auto str() const noexcept { return m_str; }

private:
	HRSRC hResInfo{};
	HGLOBAL hResData{};
	std::string_view m_str;
};


class UsbIds
{
public:
	UsbIds(const std::string_view &content) { load(content); }

	auto operator!() const noexcept { return m_vendor.empty() || m_class.empty(); } 
	explicit operator bool() const noexcept { return !!*this; }

	void load(const std::string_view &content);

	std::pair<std::string_view, std::string_view> find_product(uint16_t vid, uint16_t pid) const noexcept;

	std::tuple<std::string_view, std::string_view, std::string_view> 
		find_class_subclass_proto(uint8_t class_id, uint8_t subclass_id, uint8_t prot_id) const noexcept;

private:
	using products_t = std::unordered_map<uint16_t, std::string_view>;
	using vendors_t = std::unordered_map<uint16_t, std::pair<std::string_view, products_t>>;
	vendors_t m_vendor;

	using proto_t = std::unordered_map<uint8_t, std::string_view>;
	using subclass_t = std::unordered_map<uint8_t, std::pair<std::string_view, proto_t>>;
	using class_t = std::unordered_map<uint8_t, std::pair<std::string_view, subclass_t>>;
	class_t m_class;

	bool parse_vid_pid(uint16_t &vid, uint16_t &pid, std::string_view &line, std::string_view &tail);
	bool parse_class_sub_proto(uint8_t &cls, uint8_t &subcls, std::string_view &line, std::string_view &tail);
};
