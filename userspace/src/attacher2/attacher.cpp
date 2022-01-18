#include <winsock2.h>
#include <windows.h>

#include <cerrno>
#include <cassert>
#include <exception>
#include <fstream>

#include <boost/asio.hpp>

#include "usbip_common.h"
#include "usbip_proto.h"
#include "usbip_vhci_api.h"

namespace
{

//using boost::asio::ip::tcp;

//using streambuf_ptr = std::shared_ptr<boost::asio::streambuf>;

using header_buf_t = std::array<char, sizeof(usbip_header)>;
using header_ptr = std::shared_ptr<header_buf_t>;

boost::asio::io_context io_ctx;

boost::asio::windows::stream_handle device(io_ctx);
boost::asio::windows::stream_handle socket(io_ctx);

auto read_handle(HANDLE hStdin)
{
	HANDLE handle{};
	auto buf = reinterpret_cast<char*>(&handle);

	for (DWORD buflen = sizeof(handle); buflen; ) {

		DWORD nread{};

		if (ReadFile(hStdin, buf + sizeof(handle) - buflen, buflen, &nread, nullptr) && nread) {
			buflen -= nread;
		} else {
			return INVALID_HANDLE_VALUE;
		}
	}

	return handle;
}

void shutdown_device(HANDLE hdev)
{
	if (hdev != INVALID_HANDLE_VALUE) {
		unsigned long unused{};
		DeviceIoControl(hdev, IOCTL_USBIP_VHCI_SHUTDOWN_HARDWARE, nullptr, 0, nullptr, 0, &unused, nullptr);
	}
}

auto read_handles()
{
	auto hStdin = GetStdHandle(STD_INPUT_HANDLE);
	assert(hStdin != INVALID_HANDLE_VALUE);

	auto hdev = read_handle(hStdin);
	auto hsock = read_handle(hStdin);

	CloseHandle(hStdin);
	return std::make_pair(hdev, hsock);
}

void on_device_read(const boost::system::error_code &err, std::size_t transferred, header_ptr hdr)
{
	std::ofstream os("attacher2.log", std::ios::out | std::ios::trunc | std::ios::binary);

	if (err) {
		os << "error\n";
	} else {
		assert(transferred == hdr->size());
		os.write((char*)hdr.get(), hdr->size());
	}
}

void run()
{
	auto hdr = std::make_shared<header_buf_t>();

	auto f = [hdr] (auto&&... args)
	{
		on_device_read(std::forward<decltype(args)>(args)..., hdr);
	};

	async_read(device, boost::asio::buffer(*hdr), f);
}

void setup_forwarder()
{
	auto [hdev, hsock] = read_handles();
	device.assign(hdev);
	socket.assign(hsock);

	if (device.is_open() && socket.is_open()) {
		run();
	}

	shutdown_device(hdev);
}

} // namespace


int main()
{
	try {
		setup_forwarder();
	} catch (std::exception &e) {
		std::ofstream os("attacher2.log", std::ios::out | std::ios::trunc);
		os << e.what() << '\n';
	}

	return EXIT_FAILURE;
}
