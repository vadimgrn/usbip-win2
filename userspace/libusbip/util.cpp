#include "util.h"

#include <vector>
#include <windows.h>

std::wstring utf8_to_wchar(const char *str)
{
        std::wstring wstr;

	auto cch = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
        if (!cch) {
                return wstr;
        }

        std::vector<wchar_t> v(cch);

        if (MultiByteToWideChar(CP_UTF8, 0, str, -1, v.data(), cch) == cch) {
		wstr.assign(v.data(), cch);
	}

	return wstr;
}
 