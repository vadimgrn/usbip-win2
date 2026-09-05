/*
 * Copyright (c) 2024-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "tree_comparator.h"
#include "device_columns.h"
#include "wxutils.h"
#include "utils.h"

#include <span>
#include <algorithm>

namespace
{

using namespace usbip;

struct busid_parts
{
        std::array<int, 8> parts{};
        size_t count{};

        constexpr bool valid() const noexcept { return count >= 2; }
        constexpr auto span() const noexcept { return std::span(parts.data(), count); }

        friend constexpr auto operator<=>(const busid_parts &a, const busid_parts &b) noexcept
        {
                return std::lexicographical_compare_three_way(
                        a.span().begin(), a.span().end(),
                        b.span().begin(), b.span().end());
        }
};

auto parse_number(_In_ std::wstring_view s) noexcept -> std::optional<int>
{
        if (s.empty()) {
                return std::nullopt;
        }

        int val = 0;
        for (auto ch : s) {
                if (ch < L'0' || ch > L'9') {
                        return std::nullopt;
                }
                val = val * 10 + (ch - L'0');
        }
        return val;
}

bool append_ports(_Inout_ busid_parts &res, _In_ std::wstring_view ports) noexcept
{
        while (!ports.empty() && res.count < res.parts.size()) {
                auto dot = ports.find(L'.');
                auto part = dot == std::wstring_view::npos ? ports : ports.substr(0, dot);

                auto val = parse_number(part);
                if (!val) {
                        return false;
                }
                res.parts[res.count++] = *val;

                if (dot == std::wstring_view::npos) {
                        break;
                }
                ports = ports.substr(dot + 1);
        }
        return true;
}

/*
 * @param busid hub-port[.port]... 
 */
auto parse_busid(_In_ std::wstring_view busid) noexcept -> busid_parts
{
        busid_parts res;

        auto hyphen = busid.find(L'-');
        if (hyphen == std::wstring_view::npos) {
                return res;
        }

        auto hub = parse_number(busid.substr(0, hyphen));
        if (!hub) {
                return res;
        }
        res.parts[res.count++] = *hub;

        if (!append_ports(res, busid.substr(hyphen + 1)) || !res.valid()) {
                res.count = 0;
        }

        return res;
}

} // namespace


int TreeListItemComparator::Compare(
        wxTreeListCtrl *tree, unsigned int column, wxTreeListItem first, wxTreeListItem second)
{
        auto left = tree->GetItemText(first, column);
        auto right = tree->GetItemText(second, column);

        if (column == COL_BUSID && tree->GetItemParent(first) != tree->GetRootItem()) {
                if (auto a = parse_busid(wstring_view(left)), b = parse_busid(wstring_view(right)); a.valid() && b.valid()) {
                        auto ret = a <=> b;
                        return ret < 0 ? -1 : (ret > 0 ? 1 : 0);
                }
        } else if (column == COL_SPEED) {
                if (auto a = get_speed_val(left), b = get_speed_val(right); a && b) {
                        auto ret = *a <=> *b;
                        return ret < 0 ? -1 : (ret > 0 ? 1 : 0);
                }
        }

        return left.Cmp(right);
}
