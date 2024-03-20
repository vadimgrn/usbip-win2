/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <sal.h>
#include <compare>
#include <wx/string.h>

class wxMenu;
class wxMenuItem;

wxMenuItem* clone_menu_item(_In_ wxMenu &dest, _In_ int item_id, _In_ const wxMenu &src);

std::strong_ordering operator <=> (_In_ const wxString &a, _In_ const wxString &b);
