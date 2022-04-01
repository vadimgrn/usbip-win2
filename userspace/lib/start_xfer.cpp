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

#include <assert.h>

static BOOL write_data(HANDLE hInWrite, const void *data, DWORD len)
{
	DWORD nwritten = 0;
	BOOL res = WriteFile(hInWrite, data, len, &nwritten, nullptr);

	if (res && nwritten == len) {
		return TRUE;
	}

	dbg("failed to write handle value, len %d", len);
	return FALSE;
}

static BOOL create_pipe(HANDLE *phRead, HANDLE *phWrite)
{
	SECURITY_ATTRIBUTES saAttr{sizeof(saAttr), nullptr, TRUE};

	if (CreatePipe(phRead, phWrite, &saAttr, 0)) {
		return TRUE;
	}

	dbg("failed to create stdin pipe: 0x%lx", GetLastError());
	return FALSE;
}

static bool get_usbip_xfer_path(char *path, DWORD cch)
{
        DWORD n = GetModuleFileName(nullptr, path, cch);
        if (!(n > 0 && n < cch)) {
                dbg("GetModuleFileName error %#0x", GetLastError());
                return false;
        }

        LPSTR fname = nullptr;
        n = GetFullPathName(path, cch, path, &fname);
        assert(n > 0 && n < cch);

        strcpy_s(fname, cch - (fname - path), usbip_xfer_binary());
        return true;
}

int start_xfer(HANDLE hdev, SOCKET sockfd, bool client)
{
        int ret = ERR_GENERAL;

        usbip_xfer_args args{ INVALID_HANDLE_VALUE };
        args.client = client;

        HANDLE hRead;
	HANDLE hWrite;
	if (!create_pipe(&hRead, &hWrite)) {
		return ERR_GENERAL;
	}

	char CommandLine[MAX_PATH];
        if (!get_usbip_xfer_path(CommandLine, ARRAYSIZE(CommandLine))) {
                return ERR_GENERAL;
        }

	STARTUPINFO si{sizeof(si)};
	si.hStdInput = hRead,
	si.dwFlags = STARTF_USESTDHANDLES;

	PROCESS_INFORMATION pi{};
        BOOL res = CreateProcess(nullptr, CommandLine, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
	if (!res) {
		DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND) {
			ret = ERR_NOTEXIST;
		}
		dbg("failed to create process: 0x%lx", err);
		goto out;
	}

	res = DuplicateHandle(GetCurrentProcess(), hdev, pi.hProcess, &args.hdev, 0, FALSE, DUPLICATE_SAME_ACCESS);
	if (!res) {
		dbg("failed to dup hdev: 0x%lx", GetLastError());
		goto out_proc;
	}

	res = WSADuplicateSocketW(sockfd, pi.dwProcessId, &args.info);
	if (res) {
		dbg("failed to dup sockfd: %#x", WSAGetLastError());
		goto out_proc;
	}

	if (write_data(hWrite, &args, sizeof(args))) {
		ret = 0;
	}

out_proc:
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
out:
	CloseHandle(hRead);
	CloseHandle(hWrite);

	return ret;
}