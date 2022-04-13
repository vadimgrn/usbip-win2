/*
* Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
*               2005-2007 Takahiro Hirofuchi
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "start_xfer.h"
#include "usbip_xfer/usbip_xfer.h"
#include "usbip_network.h"
#include "win_handle.h"
#include "usbip_util.h"

#include <cassert>

namespace
{

BOOL write_data(HANDLE hInWrite, const void *data, DWORD len)
{
        DWORD nwritten = 0;
        BOOL res = WriteFile(hInWrite, data, len, &nwritten, nullptr);

        if (res && nwritten == len) {
                return TRUE;
        }

        dbg("failed to write handle value, len %d", len);
        return FALSE;
}

auto create_pipe()
{
        std::pair<usbip::Handle, usbip::Handle> p;

        HANDLE hRead;
        HANDLE hWrite;
        SECURITY_ATTRIBUTES saAttr{ sizeof(saAttr), nullptr, TRUE };

        if (CreatePipe(&hRead, &hWrite, &saAttr, 0)) {
                p.first.reset(hRead);
                p.second.reset(hWrite);
        }

        dbg("failed to create stdin pipe: 0x%lx", GetLastError());
        return p;
}

} // namespace


int start_xfer(HANDLE hdev, SOCKET sockfd, bool client)
{
        int ret = ERR_GENERAL;
        
        usbip_xfer_args args{ INVALID_HANDLE_VALUE };
        args.client = client;

        auto pipe = create_pipe();
        if (!(pipe.first && pipe.second)) {
		return ret;
	}

        auto exe_path = get_module_dir() + usbip_xfer_binary;
        
        char CommandLine[MAX_PATH];
        strncpy_s(CommandLine, exe_path.c_str(), MAX_PATH);

	STARTUPINFO si{sizeof(si)};
	si.hStdInput = pipe.first.get(), // read
	si.dwFlags = STARTF_USESTDHANDLES;

	PROCESS_INFORMATION pi{};
        auto res = CreateProcess(nullptr, CommandLine, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
	if (!res) {
		auto err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND) {
			ret = ERR_NOTEXIST;
		}
		dbg("failed to create process: 0x%lx", err);
		return ret;
	}

        usbip::Handle hProcess(pi.hProcess);
        usbip::Handle hThread(pi.hThread);

	res = DuplicateHandle(GetCurrentProcess(), hdev, pi.hProcess, &args.hdev, 0, FALSE, DUPLICATE_SAME_ACCESS);
	if (!res) {
		dbg("failed to dup hdev: 0x%lx", GetLastError());
                return ret;
        }

        usbip::Handle args_hdev(args.hdev);
        
        res = WSADuplicateSocketW(sockfd, pi.dwProcessId, &args.info);
	if (res) {
		dbg("failed to dup sockfd: %#x", WSAGetLastError());
                return ret;
        }

	if (write_data(pipe.second.get(), &args, sizeof(args))) {
                args_hdev.release();
		ret = 0;
	}

	return ret;
}
