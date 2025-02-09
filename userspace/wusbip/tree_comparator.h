/*
 * Copyright (C) 2024 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/treelist.h>

class TreeListItemComparator : public wxTreeListItemComparator
{
public:
        int Compare(wxTreeListCtrl *tree, unsigned int column, wxTreeListItem first, wxTreeListItem second) override;
};
