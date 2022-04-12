#pragma once

#include <string>

std::string get_module_dir(); 
std::wstring utf8_to_wchar(const char *str);

inline auto utf8_to_wchar(const std::string &s)
{
        return utf8_to_wchar(s.c_str());
}

constexpr auto snprintf_ok(int result, size_t buf_sz) noexcept
{
        return result > 0 && static_cast<size_t>(result) < buf_sz;
}