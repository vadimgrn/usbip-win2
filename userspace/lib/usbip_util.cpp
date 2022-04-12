#include "usbip_util.h"
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

std::string get_module_dir()
{
        std::string path;

        char buf[MAX_PATH];
        auto cnt = GetModuleFileName(nullptr, buf, MAX_PATH);
        if (!(cnt > 0 && cnt < MAX_PATH)) {
                return path;
        }

        if (auto pos = strrchr(buf, '\\')) {
                *pos = '\0';
        }
        
        return path = buf;
} 