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

/*
 * The last char of returned path is '\\'.
 */
std::string get_module_dir()
{
        std::string path;
        std::vector<char> v(MAX_PATH);

        while (true) {

                auto cnt = GetModuleFileName(nullptr, v.data(), static_cast<DWORD>(v.size()));

                if (cnt > 0 && cnt < v.size()) {
                        path.assign(v.data(), cnt);
                        auto pos = path.find_last_of('\\');
                        if (pos != path.npos) {
                                path.resize(++pos);
                        }
                        break;
                } else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                        v.resize(2*v.size());
                } else {
                        break;
                }
        }

        return path;
} 