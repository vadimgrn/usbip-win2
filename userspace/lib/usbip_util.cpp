#include "usbip_util.h"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
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

int
vasprintf(char **strp, const char *fmt, va_list ap)
{
	size_t	size;
	int	ret;

	int len = _vscprintf(fmt, ap);
	if (len == -1) {
		return -1;
	}
	size = (size_t)len + 1;
	auto str = (char*)malloc(size);
	if (!str) {
		return -1;
	}
	ret = vsprintf_s(str, len + 1, fmt, ap);
	if (ret == -1) {
		free(str);
		return -1;
	}
	*strp = str;
	return ret;
}

int
asprintf(char **strp, const char *fmt, ...)
{
	va_list	ap;
	int	ret;

	va_start(ap, fmt);
	ret = vasprintf(strp, fmt, ap);
	va_end(ap);
	return ret;
}

std::string get_module_dir()
{
        std::string path;

        char buf[MAX_PATH];
        auto cnt = GetModuleFileName(nullptr, buf, MAX_PATH);
        if (!(cnt > 0 && cnt < MAX_PATH)) {
                return path;
        }

        if (auto last_sep = strrchr(buf, '\\')) {
                *last_sep = '\0';
        }
        
        return path = buf;
} 