/*
 * Copyright (c) 2024-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "tree_comparator.h"
#include "device_columns.h"
#include "wxutils.h"
#include "utils.h"

namespace
{

using namespace usbip;

/*
 * @param busid hub-port[.port]... 
 */
auto parse_busid(_In_ const wxString &busid)
{
        std::vector<int> v;

        if (busid.length() < 3) { // "1-1"
                return v;
        }

        v.reserve(4);

        int val{};
        bool has_val{};
        auto sep = L'-';

        for (auto ch : busid) {
                if (usbip::isdigit(ch)) {
                        val = 10*val + (ch - L'0');
                        has_val = true;
                } else if (ch == sep && has_val) {
                        v.push_back(val);
                        val = 0;
                        has_val = false;
                        sep = L'.';
                } else {
                        v.clear();
                        return v;
                }
        }

        if (has_val && sep == L'.') {
                v.push_back(val);
        } else {
                v.clear();
        }

        return v;
}

} // namespace


int TreeListItemComparator::Compare(
        wxTreeListCtrl *tree, unsigned int column, wxTreeListItem first, wxTreeListItem second)
{
        auto &left = tree->GetItemText(first, column);
        auto &right = tree->GetItemText(second, column);

        if (column == COL_BUSID && tree->GetItemParent(first) != tree->GetRootItem()) {
                if (auto a = parse_busid(left), b = parse_busid(right); !(a.empty() || b.empty())) {
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
