/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usb_ids.h"

#include <cassert>
#include <cerrno>
#include <functional>

namespace
{

uint16_t remove_prefix_hex(std::string_view &s)
{
        char *end{};

        errno = 0;
        auto n = strtol(s.data(), &end, 16); // FIXME: doesn't respect s.size()

        if (errno || end == s.data()) {
		return 0;
	}

	size_t cnt = end - s.data();
        if (cnt > s.size()) {
                return 0;
        }

        s.remove_prefix(cnt);
        return static_cast<uint16_t>(n);
}

using line_f = std::function<bool(std::string_view&, std::string_view&)>;

void for_each_line(std::string_view text, const line_f &f)
{
        while (!text.empty()) {
                auto pos = text.find('\n');
                if (pos == text.npos) {
                        std::string_view tail;
                        f(text, tail);
                        break;
                }

                auto line = text.substr(0, pos ? pos - 1 : 0); // rstrip '\n'
                text.remove_prefix(++pos);

                if (!line.empty() && f(line, text)) {
                        break;
                }
        }
}

} // namespace


DWORD Resource::load(_In_opt_ HMODULE hModule, _In_ LPCSTR name, _In_ LPCSTR type)
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

void UsbIds::load(const std::string_view &content)
{
        uint16_t vid{};
        uint16_t pid{};
        
        auto f = [this, &vid, &pid] (auto&&... args) 
        { 
                return parse_vid_pid(vid, pid, std::forward<decltype(args)>(args)...); 
        };
        
        for_each_line(content, std::move(f));
}

bool UsbIds::parse_vid_pid(uint16_t &vid, uint16_t &pid, std::string_view &line, std::string_view &tail)
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
        } else if (line.starts_with("\t\t")) {
                assert(!"\\t\\t detected");
        } else if (line.starts_with('\t')) {
                line.remove_prefix(1);
                if (bool(pid = remove_prefix_hex(line))) {
                        line.remove_prefix(2); // device_name
                        auto &prod = m_vendor[vid].second;
                        auto [it, inserted] = prod.emplace(pid, line);
                        assert(inserted);
                }
        } else if (bool(vid = remove_prefix_hex(line))) {
                line.remove_prefix(2); // vendor_name
                auto [it, inserted] = m_vendor.emplace(vid, std::make_pair(line, products_t()));
                assert(inserted);
        }

        return false;
}

bool UsbIds::parse_class_sub_proto(uint8_t &cls, uint8_t &subcls, std::string_view &line, std::string_view&)
{
        if (line.starts_with("# List of Audio Class Terminal Types")) {
                return true;
        } else if (line.starts_with('#')) {
                // continue;
        } else if (line.starts_with("\t\t")) {
                line.remove_prefix(2);
                if (auto prot = (uint8_t)remove_prefix_hex(line)) {
                        line.remove_prefix(2);
                        auto &sub = m_class[cls].second;
                        auto &proto = sub[subcls].second;
                        auto [it, inserted] = proto.emplace(prot, line);
                        assert(inserted);
                }
        } else if (line.starts_with('\t')) {
                line.remove_prefix(1);
                if (bool(subcls = (uint8_t)remove_prefix_hex(line))) {
                        line.remove_prefix(2);
                        auto &sub = m_class[cls].second;
                        auto [it, inserted] = sub.emplace(subcls, std::make_pair(line, proto_t()));
                        assert(inserted);
                }
        } else if (line.starts_with("C ")) {
                line.remove_prefix(2);
                if (bool(cls = (uint8_t)remove_prefix_hex(line))) {
                        line.remove_prefix(2);
                        auto [it, inserted] = m_class.emplace(cls, std::make_pair(line, subclass_t()));
                        assert(inserted);
                }
        }

        return false;
}

std::pair<std::string_view, std::string_view> UsbIds::find_product(uint16_t vid, uint16_t pid) const noexcept
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
UsbIds::find_class_subclass_proto(uint8_t class_id, uint8_t subclass_id, uint8_t prot_id) const noexcept
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
