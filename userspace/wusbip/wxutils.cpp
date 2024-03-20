/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wxutils.h"
#include <wx/menu.h>

std::strong_ordering operator <=> (_In_ const wxString &a, _In_ const wxString &b)
{
        std::strong_ordering v[] { 
                std::strong_ordering::less, 
                std::strong_ordering::equal, 
                std::strong_ordering::greater 
        };

        auto i = a.Cmp(b);
        wxASSERT(i >= -1 && i <= 1);

        return v[++i];
}

wxMenuItem* clone_menu_item(_In_ wxMenu &dest, _In_ int item_id, _In_ const wxMenu &src)
{
        wxMenuItem *clone{};

        if (item_id == wxID_SEPARATOR) {
                dest.AppendSeparator();
        } else if (auto item = src.FindItem(item_id)) {
                clone = dest.Append(item_id, item->GetItemLabel(), item->GetHelp(), item->GetKind());
                for (auto checked: {false, true}) {
                        clone->SetBitmap(item->GetBitmap(checked), checked);
                }
        } else {
                wxFAIL_MSG("FindItem");
        }

        return clone;
}
