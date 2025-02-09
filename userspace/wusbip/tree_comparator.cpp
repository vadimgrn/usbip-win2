/*
 * Copyright (C) 2024 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
auto parse_busid(_In_ wxString busid)
{
        std::vector<int> v;

        if (auto i = busid.Find(L'-'); i != wxNOT_FOUND) {
                busid[i] = L'.';
        } else {
                return v;
        }

        int val;
        for (wxString rest; busid.BeforeFirst(L'.', &rest).ToInt(&val); busid = std::move(rest)) {
                v.push_back(val);
        }

        if (v.size() < 2) { // hub-port at least
                v.clear();
        }

        return v;
}

} // namespace


int TreeListItemComparator::Compare(
        wxTreeListCtrl *tree, unsigned int column, wxTreeListItem first, wxTreeListItem second)
{
        auto left = tree->GetItemText(first, column);
        auto right = tree->GetItemText(second, column);

        if (column == COL_BUSID && tree->GetItemParent(first) != tree->GetRootItem()) {
                if (auto a = parse_busid(left), b = parse_busid(right); !(a.empty() || b.empty())) {
                        auto ret = a <=> b;
                        return ret._Value;
                }
        } else if (column == COL_SPEED) {
                if (USB_DEVICE_SPEED a, b; get_speed_val(a, left) && get_speed_val(b, right)) {
                        auto ret = a <=> b;
                        return ret._Value;
                }
        }

        return left.Cmp(right);
}
