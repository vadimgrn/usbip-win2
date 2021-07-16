#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "usbip_forward.h"
#include "usbip_vhci_api.h"

static HANDLE
read_handle_value(HANDLE hStdin)
{
	HANDLE	handle;
	LPBYTE	buf = (LPBYTE)&handle;
	DWORD	buflen = sizeof(HANDLE);

	while (buflen > 0) {
		DWORD	nread;

		if (!ReadFile(hStdin, buf + sizeof(HANDLE) - buflen, buflen, &nread, NULL)) {
			return INVALID_HANDLE_VALUE;
		}
		if (nread == 0)
			return INVALID_HANDLE_VALUE;
		buflen -= nread;
	}
	return handle;
}

static void
shutdown_device(HANDLE hdev)
{
	unsigned long	unused;

	DeviceIoControl(hdev, IOCTL_USBIP_VHCI_SHUTDOWN_HARDWARE, NULL, 0, NULL, 0, &unused, NULL);
}

static BOOL
setup_forwarder(void)
{
	HANDLE	hdev, sockfd;
	HANDLE	hStdin, hStdout;

	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

	hdev = read_handle_value(hStdin);
	sockfd = read_handle_value(hStdin);

	usbip_forward(hdev, sockfd, FALSE);
	shutdown_device(hdev);

	CloseHandle(sockfd);
	CloseHandle(hdev);

	CloseHandle(hStdin);
	CloseHandle(hStdout);

	return TRUE;
}

int APIENTRY
wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
	 _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	if (!setup_forwarder())
		return 1;

	return 0;
}