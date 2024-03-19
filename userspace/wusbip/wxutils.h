/*
 * Copyright (C) 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <sal.h>

class wxMenu;

bool clone_menu_item(_In_ wxMenu &dest, _In_ int item_id, _In_ const wxMenu &src);