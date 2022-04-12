#pragma once

#include <string>
#include <cstdarg>

std::string get_module_dir(); 
std::wstring utf8_to_wchar(const char *str);

int vasprintf(char **strp, const char *fmt, va_list ap);

constexpr auto snprintf_ok(int result, size_t buf_sz) noexcept
{
        return result > 0 && static_cast<size_t>(result) < buf_sz;
}