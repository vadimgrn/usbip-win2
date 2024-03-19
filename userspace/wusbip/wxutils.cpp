/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wxutils.h"
#include <wx/menu.h>

bool clone_menu_item(_In_ wxMenu &dest, _In_ int item_id, _In_ const wxMenu &src)
{
        bool bind{};

        if (item_id == wxID_SEPARATOR) {
                dest.AppendSeparator();
        } else if (auto item = src.FindItem(item_id)) {
                auto clone = dest.Append(item_id, item->GetItemLabel(), item->GetHelp(), item->GetKind());
                for (auto checked: {false, true}) {
                        clone->SetBitmap(item->GetBitmap(checked), checked);
                }
                bind = true;
        } else {
                wxFAIL_MSG("FindItem");
        }

        return bind;
}
