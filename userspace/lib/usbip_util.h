#pragma once

#include <string>
#include <cstdarg>

std::string get_module_dir(); 

std::wstring utf8_to_wchar(const char *str);

int vasprintf(char **strp, const char *fmt, va_list ap);
int asprintf(char **strp, const char *fmt, ...);