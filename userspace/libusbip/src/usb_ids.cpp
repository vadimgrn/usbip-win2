/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usb_ids.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

template<std::integral T>
constexpr auto consume_hex(std::string_view &s, size_t count) noexcept -> std::optional<T>
{
	if (s.size() < count) {
		return std::nullopt;
	}

	uint32_t val{};
	auto [ptr, ec] = std::from_chars(s.data(), s.data() + count, val, 16);
	if (!(ec == std::errc{} && ptr == s.data() + count)) {
		return std::nullopt;
	}

	s.remove_prefix(count);
	return static_cast<T>(val);
}

constexpr auto consume_name_prefix(std::string_view &s) noexcept -> bool
{
	if (s.starts_with("  ")) {
		s.remove_prefix(2);
		return true;
	}

	if (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
			s.remove_prefix(1);
		}
		return true;
	}

	return false;
}

constexpr auto trim_line(auto &&raw_line) noexcept -> std::string_view
{
	std::string_view line(raw_line);
	if (line.ends_with('\r')) {
		line.remove_suffix(1);
	}
	return line;
}

enum class ParseSection : uint8_t { Vendors, Classes, Done };

struct ProductEntry
{
	uint16_t pid{};
	std::string_view name;

	constexpr ProductEntry() noexcept = default;
	constexpr ProductEntry(uint16_t id, std::string_view n) noexcept : pid(id), name(n) {}
};

struct VendorEntry
{
	uint32_t product_start{};
	uint32_t product_count{};
	uint16_t vid{};
	std::string_view name;

	constexpr VendorEntry() noexcept = default;
	constexpr VendorEntry(uint16_t v, std::string_view n, uint32_t start, uint32_t count = 0) noexcept
		: product_start(start), product_count(count), vid(v), name(n) {}
};

struct ProtocolEntry
{
	uint8_t id{};
	std::string_view name;

	constexpr ProtocolEntry() noexcept = default;
	constexpr ProtocolEntry(uint8_t i, std::string_view n) noexcept : id(i), name(n) {}
};

struct SubclassEntry
{
	uint32_t protocol_start{};
	uint32_t protocol_count{};
	uint8_t id{};
	std::string_view name;

	constexpr SubclassEntry() noexcept = default;
	constexpr SubclassEntry(uint8_t i, std::string_view n, uint32_t start, uint32_t count = 0) noexcept
		: protocol_start(start), protocol_count(count), id(i), name(n) {}
};

struct ClassEntry
{
	uint32_t subclass_start{};
	uint32_t subclass_count{};
	uint8_t id{};
	std::string_view name;

	constexpr ClassEntry() noexcept = default;
	constexpr ClassEntry(uint8_t i, std::string_view n, uint32_t start, uint32_t count = 0) noexcept
		: subclass_start(start), subclass_count(count), id(i), name(n) {}
};

} // namespace


class win::Resource::Impl
{
public:
	Impl(_In_opt_ HMODULE hModule, _In_ LPCTSTR name, _In_ LPCTSTR type) { load(hModule, name, type); }

	explicit operator bool() const noexcept { return hResInfo && hResData; }
	auto operator!() const noexcept { return !bool(*this); } 

	DWORD load(_In_opt_ HMODULE hModule, _In_ LPCTSTR name, _In_ LPCTSTR type);

	auto data() const noexcept { return LockResource(hResData); }
	auto size(HMODULE hModule) const noexcept { return SizeofResource(hModule, hResInfo); }

	auto str() const noexcept { return m_str; }

private:
	HRSRC hResInfo{};
	HGLOBAL hResData{};
	std::string_view m_str;
};

DWORD win::Resource::Impl::load(_In_opt_ HMODULE hModule, _In_ LPCTSTR name, _In_ LPCTSTR type)
{
	hResInfo = FindResource(hModule, name, type);
	if (!hResInfo) {
		return GetLastError();
	}

	hResData = LoadResource(hModule, hResInfo);
	if (!hResData) {
		return GetLastError();
	}

	m_str = std::string_view(reinterpret_cast<const char*>(data()), size(hModule));
	return ERROR_SUCCESS;
}

win::Resource::Resource(_In_opt_ HMODULE hModule, _In_ LPCTSTR name, _In_ LPCTSTR type) 
	: m_impl(new Impl(hModule, name, type)) {}

win::Resource::~Resource() { delete m_impl; }

auto win::Resource::operator =(Resource&& obj) noexcept -> Resource&
{
	if (&obj != this) {
		delete m_impl;
		m_impl = obj.release();
	}

	return *this;
}

win::Resource::operator bool() const noexcept { return m_impl && static_cast<bool>(*m_impl); }

DWORD win::Resource::load(_In_opt_ HMODULE hModule, _In_ LPCTSTR name, _In_ LPCTSTR type) 
{
	return m_impl->load(hModule, name, type); 
}

void* win::Resource::data() const noexcept { return m_impl->data(); }
DWORD win::Resource::size(HMODULE hModule) const noexcept { return m_impl->size(hModule); }
std::string_view win::Resource::str() const noexcept { return m_impl->str(); }


class usbip::UsbIds::Impl
{
public:
	explicit Impl(std::string_view content) { load(content); }

	[[nodiscard]] explicit operator bool() const noexcept {
		return !m_vendors.empty() && !m_classes.empty();
	}

	[[nodiscard]] bool operator!() const noexcept {
		return !bool(*this);
	}

	void load(std::string_view content);

	[[nodiscard]] std::pair<std::string_view, std::string_view> 
	find_product(uint16_t vid, uint16_t pid) const noexcept;

	[[nodiscard]] std::tuple<std::string_view, std::string_view, std::string_view> 
	find_class_subclass_proto(uint8_t class_id, uint8_t subclass_id, uint8_t prot_id) const noexcept;

private:
	std::vector<VendorEntry> m_vendors;
	std::vector<ProductEntry> m_products;

	std::vector<ClassEntry> m_classes;
	std::vector<SubclassEntry> m_subclasses;
	std::vector<ProtocolEntry> m_protocols;

	void reset();
	bool handle_comment(std::string_view line, ParseSection &section);

	void parse_vendor_section_line(std::string_view line);
	void parse_vendor(std::string_view line);
	void parse_product(std::string_view line);

	void parse_class_section_line(std::string_view line);
	void parse_class(std::string_view line);
	void parse_subclass(std::string_view line);
	void parse_protocol(std::string_view line);

	void finalize_vendor();
	void finalize_subclass();
	void finalize_class();
	void finalize_all(ParseSection section);

	void ensure_vendors_sorted();
	void ensure_classes_sorted();
	void ensure_subclasses_sorted();
	void ensure_sorted();
};

void usbip::UsbIds::Impl::reset()
{
	m_vendors.clear();
	m_products.clear();
	m_classes.clear();
	m_subclasses.clear();
	m_protocols.clear();

        enum {
                Vendors = 4096,
                Products = 32768,
                Classes = 64,
                Subclasses = 256,
                Protocols = 256,
        };

        m_vendors.reserve(Vendors);
        m_products.reserve(Products);
        m_classes.reserve(Classes);
        m_subclasses.reserve(Subclasses);
        m_protocols.reserve(Protocols);
}

void usbip::UsbIds::Impl::finalize_vendor()
{
	if (!m_vendors.empty()) {
		m_vendors.back().product_count = 
			static_cast<uint32_t>(m_products.size() - m_vendors.back().product_start);
	}
}

void usbip::UsbIds::Impl::finalize_subclass()
{
	if (!m_subclasses.empty()) {
		m_subclasses.back().protocol_count = 
			static_cast<uint32_t>(m_protocols.size() - m_subclasses.back().protocol_start);
	}
}

void usbip::UsbIds::Impl::finalize_class()
{
	finalize_subclass();
	if (!m_classes.empty()) {
		m_classes.back().subclass_count = 
			static_cast<uint32_t>(m_subclasses.size() - m_classes.back().subclass_start);
	}
}

void usbip::UsbIds::Impl::finalize_all(ParseSection section)
{
	if (section == ParseSection::Vendors) {
		finalize_vendor();
	} else if (section == ParseSection::Classes) {
		finalize_class();
	}
}

bool usbip::UsbIds::Impl::handle_comment(std::string_view line, ParseSection &section)
{
	if (section == ParseSection::Vendors) {
		if (line.starts_with("# List of known device classes, subclasses and protocols")) {
			finalize_vendor();
			section = ParseSection::Classes;
		}
	} else if (section == ParseSection::Classes) {
		if (line.starts_with("# List of Audio Class Terminal Types") ||
		    line.starts_with("# List of HID") ||
		    line.starts_with("# List of Physical") ||
		    line.starts_with("# List of Languages")) {
			finalize_class();
			section = ParseSection::Done;
			return true;
		}
	}

	return false;
}

void usbip::UsbIds::Impl::parse_product(std::string_view line)
{
	if (auto pid = consume_hex<uint16_t>(line, 4); pid && consume_name_prefix(line)) {
		if (!m_vendors.empty()) {
			m_products.emplace_back(*pid, line);
		}
	}
}

void usbip::UsbIds::Impl::parse_vendor(std::string_view line)
{
	if (auto vid = consume_hex<uint16_t>(line, 4); vid && consume_name_prefix(line)) {
		finalize_vendor();
		m_vendors.emplace_back(*vid, line, static_cast<uint32_t>(m_products.size()));
	}
}

void usbip::UsbIds::Impl::parse_vendor_section_line(std::string_view line)
{
	if (line.starts_with("\t\t")) {
		// interface sub-entry: ignore
	} else if (line.starts_with('\t')) {
		parse_product(line.substr(1));
	} else {
		parse_vendor(line);
	}
}

void usbip::UsbIds::Impl::parse_protocol(std::string_view line)
{
	if (auto prot_id = consume_hex<uint8_t>(line, 2);
            prot_id && consume_name_prefix(line) && !m_subclasses.empty()) {
		m_protocols.emplace_back(*prot_id, line);
	}
}

void usbip::UsbIds::Impl::parse_subclass(std::string_view line)
{
	if (auto subcls_id = consume_hex<uint8_t>(line, 2); subcls_id && consume_name_prefix(line)) {
		finalize_subclass();
		if (!m_classes.empty()) {
			m_subclasses.emplace_back(*subcls_id, line, static_cast<uint32_t>(m_protocols.size()));
		}
	}
}

void usbip::UsbIds::Impl::parse_class(std::string_view line)
{
	if (auto cls_id = consume_hex<uint8_t>(line, 2); cls_id && consume_name_prefix(line)) {
		finalize_class();
		m_classes.emplace_back(*cls_id, line, static_cast<uint32_t>(m_subclasses.size()));
	}
}

void usbip::UsbIds::Impl::parse_class_section_line(std::string_view line)
{
	if (line.starts_with("\t\t")) {
		parse_protocol(line.substr(2));
	} else if (line.starts_with('\t')) {
		parse_subclass(line.substr(1));
	} else if (line.starts_with("C ")) {
		parse_class(line.substr(2));
	}
}

void usbip::UsbIds::Impl::ensure_vendors_sorted()
{
	if (!std::ranges::is_sorted(m_vendors, {}, &VendorEntry::vid)) {
		std::ranges::sort(m_vendors, {}, &VendorEntry::vid);
	}

	for (const auto &v : m_vendors) {
		if (v.product_count > 1) {
			auto prod_span = std::span{m_products.data() + v.product_start, v.product_count};
			if (!std::ranges::is_sorted(prod_span, {}, &ProductEntry::pid)) {
				std::ranges::sort(prod_span, {}, &ProductEntry::pid);
			}
		}
	}
}

void usbip::UsbIds::Impl::ensure_classes_sorted()
{
	if (!std::ranges::is_sorted(m_classes, {}, &ClassEntry::id)) {
		std::ranges::sort(m_classes, {}, &ClassEntry::id);
	}

	for (const auto &c : m_classes) {
		if (c.subclass_count > 1) {
			auto sub_span = std::span{m_subclasses.data() + c.subclass_start, c.subclass_count};
			if (!std::ranges::is_sorted(sub_span, {}, &SubclassEntry::id)) {
				std::ranges::sort(sub_span, {}, &SubclassEntry::id);
			}
		}
	}
}

void usbip::UsbIds::Impl::ensure_subclasses_sorted()
{
	for (const auto &s : m_subclasses) {
		if (s.protocol_count > 1) {
			auto proto_span = std::span{m_protocols.data() + s.protocol_start, s.protocol_count};
			if (!std::ranges::is_sorted(proto_span, {}, &ProtocolEntry::id)) {
				std::ranges::sort(proto_span, {}, &ProtocolEntry::id);
			}
		}
	}
}

void usbip::UsbIds::Impl::ensure_sorted()
{
	ensure_vendors_sorted();
	ensure_classes_sorted();
	ensure_subclasses_sorted();
}

void usbip::UsbIds::Impl::load(std::string_view content)
{
	reset();

	auto section = ParseSection::Vendors;

	for (auto&& raw_line : std::views::split(content, '\n')) {
		auto line = trim_line(raw_line);
		if (line.empty()) {
			continue;
		}

		if (line.starts_with('#')) {
			if (handle_comment(line, section)) {
				break;
			}
			continue;
		}

		switch (section) {
		case ParseSection::Vendors:
			parse_vendor_section_line(line);
			break;
		case ParseSection::Classes:
			parse_class_section_line(line);
			break;
		case ParseSection::Done:
			break;
		}
	}

	finalize_all(section);
	ensure_sorted();
}

std::pair<std::string_view, std::string_view> 
usbip::UsbIds::Impl::find_product(uint16_t vid, uint16_t pid) const noexcept
{
	auto it = std::ranges::lower_bound(m_vendors, vid, {}, &VendorEntry::vid);
	if (it == m_vendors.end() || it->vid != vid) {
		return {};
	}

	auto vendor_name = it->name;
	if (!it->product_count) {
		return {vendor_name, {}};
	}

	std::span products{m_products.data() + it->product_start, it->product_count};
	auto pit = std::ranges::lower_bound(products, pid, {}, &ProductEntry::pid);

        if (pit != products.end() && pit->pid == pid) {
		return {vendor_name, pit->name};
	}

	return {vendor_name, {}};
}

std::tuple<std::string_view, std::string_view, std::string_view> 
usbip::UsbIds::Impl::find_class_subclass_proto(
	uint8_t class_id, uint8_t subclass_id, uint8_t prot_id) const noexcept
{
	auto cit = std::ranges::lower_bound(m_classes, class_id, {}, &ClassEntry::id);
	if (cit == m_classes.end() || cit->id != class_id) {
		return {};
	}

	auto class_name = cit->name;
	if (!cit->subclass_count) {
		return {class_name, {}, {}};
	}

	std::span subclasses{m_subclasses.data() + cit->subclass_start, cit->subclass_count};
	auto sit = std::ranges::lower_bound(subclasses, subclass_id, {}, &SubclassEntry::id);

        if (sit == subclasses.end() || sit->id != subclass_id) {
		return {class_name, {}, {}};
	}

	auto subclass_name = sit->name;
	if (!sit->protocol_count) {
		return {class_name, subclass_name, {}};
	}

	std::span protocols{m_protocols.data() + sit->protocol_start, sit->protocol_count};
	auto pit = std::ranges::lower_bound(protocols, prot_id, {}, &ProtocolEntry::id);

        if (pit != protocols.end() && pit->id == prot_id) {
		return {class_name, subclass_name, pit->name};
	}

	return {class_name, subclass_name, {}};
}


usbip::UsbIds::UsbIds(std::string_view content) 
	: m_impl(new Impl(content)) 
{
}

usbip::UsbIds::~UsbIds() 
{ 
	delete m_impl; 
}

auto usbip::UsbIds::operator =(UsbIds&& obj) noexcept -> UsbIds&
{
	if (&obj != this) {
		delete m_impl;
		m_impl = obj.release();
	}

	return *this;
}

usbip::UsbIds::operator bool() const noexcept 
{ 
	return m_impl && static_cast<bool>(*m_impl); 
}

bool usbip::UsbIds::operator !() const noexcept 
{ 
	return !bool(*this); 
}

void usbip::UsbIds::load(std::string_view content) 
{ 
	if (m_impl) {
		m_impl->load(content);
	} else {
		m_impl = new Impl(content);
	}
}

std::pair<std::string_view, std::string_view> 
usbip::UsbIds::find_product(uint16_t vid, uint16_t pid) const noexcept 
{ 
	return m_impl ? m_impl->find_product(vid, pid) : std::pair<std::string_view, std::string_view>{}; 
}

std::tuple<std::string_view, std::string_view, std::string_view> 
usbip::UsbIds::find_class_subclass_proto(uint8_t class_id, uint8_t subclass_id, uint8_t prot_id) const noexcept
{
	return m_impl ? m_impl->find_class_subclass_proto(class_id, subclass_id, prot_id)
	              : std::tuple<std::string_view, std::string_view, std::string_view>{};
}
