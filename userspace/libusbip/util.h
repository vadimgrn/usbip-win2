#pragma once

#include <string>

std::wstring utf8_to_wchar(const char *str);

inline auto utf8_to_wchar(const std::string &s)
{
        return utf8_to_wchar(s.c_str());
}
