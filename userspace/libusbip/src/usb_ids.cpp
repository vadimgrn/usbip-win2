/*
 * Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usb_ids.h"
#include "output.h"

#include <cassert>
#include <charconv>
#include <functional>

namespace
{

uint16_t remove_prefix_hex(std::string_view &s, int cnt)
{
        int val{};
        auto end = s.data() + cnt;

        auto [ptr, ec] = std::from_chars(s.data(), end, val, 16);

        if (ec != std::errc{}) {
		return 0;
	}

        assert(ptr == end);
        s.remove_prefix(cnt);

        auto res = static_cast<uint16_t>(val);
        assert(res == val);

        return res;
}

using line_f = std::function<bool(std::string_view&, std::string_view&)>;

void for_each_line(std::string_view text, const line_f &f)
{
        while (!text.empty()) {
                auto pos = text.find('\n'); // usb.ids must be in Unix format, 0xA line endings
                if (pos == text.npos) {
                        std::string_view tail;
                        f(text, tail);
                        break;
                }

                auto line = text.substr(0, pos);
                text.remove_prefix(pos + 1); // line + '\n'

                if (!line.empty() && f(line, text)) {
                        break;
                }
        }
}

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

win::Resource::operator bool() const noexcept { return static_cast<bool>(*m_impl); }

DWORD win::Resource::load(_In_opt_ HMODULE hModule, _In_ LPCTSTR name, _In_ LPCTSTR type) 
{
        return m_impl->load(hModule, name, type); 
}

void* win::Resource::data() const noexcept { return m_impl->data(); }
DWORD win::Resource::size(HMODULE hModule) const noexcept{ return m_impl->size(hModule); }
std::string_view win::Resource::str() const noexcept { return m_impl->str(); }


class usbip::UsbIds::Impl
{
public:
        Impl(std::string_view content);

        auto operator!() const noexcept { return m_vendor.empty() || m_class.empty(); } 
        explicit operator bool() const noexcept { return !!*this; }

        void load(std::string_view content);

        void dump_vendors() const;
        void dump_classes() const;

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

usbip::UsbIds::Impl::Impl(std::string_view content)
{
        load(content);
        
        //dump_vendors();
        //printf("CLASSES\n");
        //dump_classes();
}

void usbip::UsbIds::Impl::load(std::string_view content)
{
        uint16_t vid{};
        uint16_t pid{};
        
        auto f = [this, &vid, &pid] (auto&&... args) 
        { 
                return parse_vid_pid(vid, pid, std::forward<decltype(args)>(args)...); 
        };
        
        for_each_line(content, std::move(f));
}

void usbip::UsbIds::Impl::dump_vendors() const
{
        for (auto& [vid, pair]: m_vendor) {

                auto& [vendor_name, products] = pair;
                libusbip::output("{:04x}  {}", vid, vendor_name);

                for (auto& [pid, name]: products) {
                        libusbip::output("\t{:04x}  {}", pid, name);
                }
        }
}

void usbip::UsbIds::Impl::dump_classes() const
{
        for (auto& [cls_id, pair]: m_class) {

                auto& [cls_name, subclasses] = pair;
                libusbip::output("C {:02x}  {}", cls_id, cls_name);

                for (auto& [sub_id, sub_pair]: subclasses) {

                        auto& [sub_name, protocols] = sub_pair;
                        libusbip::output("\t{:02x}  {}", sub_id, sub_name);

                        for (auto& [prot_id, name]: protocols) {
                                libusbip::output("\t\t{:02x}  {}", prot_id, name);
                        }
                }
        }
}

bool usbip::UsbIds::Impl::parse_vid_pid(
        uint16_t &vid, uint16_t &pid, std::string_view &line, std::string_view &tail)
{
        if (line.starts_with("# List of known device classes, subclasses and protocols")) {
                uint8_t cls{};
                uint8_t subcls{};
                auto f = [this, &cls, &subcls] (auto&&... args) 
                { 
                        return parse_class_sub_proto(cls, subcls, std::forward<decltype(args)>(args)...); 
                };
                for_each_line(tail, std::move(f));
                return true;
        } else if (line.starts_with('#')) {
                // continue;
        } else if (line.starts_with("\t\t")) { // \t \t interface  interface_name
                assert(!"\\t\\t detected");
        } else if (line.starts_with('\t')) { // \t device  device_name
                line.remove_prefix(1);
                if (bool(pid = remove_prefix_hex(line, 4))) {
                        line.remove_prefix(2); // device_name
                        auto &prod = m_vendor[vid].second;
                        auto [it, inserted] = prod.emplace(pid, line);
                        assert(inserted);
                }
        } else if (bool(vid = remove_prefix_hex(line, 4))) { // vendor  vendor_name
                line.remove_prefix(2); // vendor_name
                auto [it, inserted] = m_vendor.emplace(vid, std::make_pair(line, products_t()));
                assert(inserted);
        }

        return false;
}

bool usbip::UsbIds::Impl::parse_class_sub_proto(
        uint8_t &cls, uint8_t &subcls, std::string_view &line, std::string_view&)
{
        if (line.starts_with("# List of Audio Class Terminal Types")) {
                return true;
        } else if (line.starts_with('#')) {
                // continue;
        } else if (line.starts_with("\t\t")) {
                line.remove_prefix(2);
                if (auto prot = (uint8_t)remove_prefix_hex(line, 2)) {
                        line.remove_prefix(2);
                        auto &sub = m_class[cls].second;
                        auto &proto = sub[subcls].second;
                        auto [it, inserted] = proto.emplace(prot, line);
                        assert(inserted);
                }
        } else if (line.starts_with('\t')) {
                line.remove_prefix(1);
                if (bool(subcls = (uint8_t)remove_prefix_hex(line, 2))) {
                        line.remove_prefix(2);
                        auto &sub = m_class[cls].second;
                        auto [it, inserted] = sub.emplace(subcls, std::make_pair(line, proto_t()));
                        assert(inserted);
                }
        } else if (line.starts_with("C ")) {
                line.remove_prefix(2);

                cls = (uint8_t)remove_prefix_hex(line, 2); // "C 00  (Defined at Interface level)"
                line.remove_prefix(2);

                auto [it, inserted] = m_class.emplace(cls, std::make_pair(line, subclass_t()));
                assert(inserted);
        }

        return false;
}

std::pair<std::string_view, std::string_view> 
usbip::UsbIds::Impl::find_product(uint16_t vid, uint16_t pid) const noexcept
{
        std::pair<std::string_view, std::string_view> res;

        auto v = m_vendor.find(vid);
        if (v == m_vendor.end()) {
                return res;
        }

        res.first = v->second.first;
        auto &prod = v->second.second;

        auto p = prod.find(pid);
        if (p != prod.end()) {
                res.second = p->second;
        }

        return res;
}

std::tuple<std::string_view, std::string_view, std::string_view> 
usbip::UsbIds::Impl::find_class_subclass_proto(
        uint8_t class_id, uint8_t subclass_id, uint8_t prot_id) const noexcept
{
        std::tuple<std::string_view, std::string_view, std::string_view>  res;

        auto c = m_class.find(class_id);
        if (c == m_class.end()) {
                return res;
        }

        std::get<0>(res) = c->second.first;
        auto &subcls = c->second.second;

        auto s = subcls.find(subclass_id);
        if (s == subcls.end()) {
                return res;
        }

        std::get<1>(res) = s->second.first;
        auto &prot = s->second.second;

        auto p = prot.find(prot_id);
        if (p != prot.end()) {
                std::get<2>(res) = p->second;
        }

        return res;
}


usbip::UsbIds::UsbIds(std::string_view content) : m_impl(new Impl(content)) {}
usbip::UsbIds::~UsbIds() { delete m_impl; }

auto usbip::UsbIds::operator =(UsbIds&& obj) noexcept -> UsbIds&
{
        if (&obj != this) {
                delete m_impl;
                m_impl = obj.release();
        }

        return *this;
}

usbip::UsbIds::operator bool() const noexcept { return static_cast<bool>(*m_impl); }
bool usbip::UsbIds::operator !() const noexcept { return !*m_impl; }

void usbip::UsbIds::load(std::string_view content) { m_impl->load(content); }

std::pair<std::string_view, std::string_view> 
usbip::UsbIds::find_product(uint16_t vid, uint16_t pid) const noexcept 
{ 
        return m_impl->find_product(vid, pid); 
}

std::tuple<std::string_view, std::string_view, std::string_view> 
usbip::UsbIds::find_class_subclass_proto(uint8_t class_id, uint8_t subclass_id, uint8_t prot_id) const noexcept
{
        return m_impl->find_class_subclass_proto(class_id, subclass_id, prot_id);
}
