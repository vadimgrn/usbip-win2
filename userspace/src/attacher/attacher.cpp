#include "trace.h"
#include "attacher.tmh"

#include <cerrno>
#include <cassert>
#include <exception>
#include <atomic>
#include <vector>

#include <boost/asio.hpp>

#include "usbip_common.h"
#include "pdu.h"
#include "usbip_vhci_api.h"

namespace
{

using boost::asio::ip::tcp;
	
std::atomic<int> g_exit_code(EXIT_SUCCESS);

struct WPPInit
{
	WPPInit() { WPP_INIT_TRACING(nullptr); }
	~WPPInit() { WPP_CLEANUP(); }
};

auto &get_io_context()
{
	static boost::asio::io_context ctx(1);
	return ctx;
}

void terminate(int exit_code = EXIT_FAILURE)
{
	auto expected = EXIT_SUCCESS;

	if (g_exit_code.compare_exchange_strong(expected, exit_code)) {
		Trace(TRACE_LEVEL_INFORMATION, "Exit code %d", exit_code);
	}

	get_io_context().stop();
}

auto terminated()
{
	return get_io_context().stopped();
}

void signal_handler(const boost::system::error_code &ec, int signum)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', signum %d", ec.value(), ec.message().c_str(), signum);
	} else {
		Trace(TRACE_LEVEL_INFORMATION, "signum %d", signum);
	}
}

class Forwarder : public std::enable_shared_from_this<Forwarder>
{
public:
	Forwarder(HANDLE hdev, SOCKET sockfd, int AddressFamily);

	void async_start();

private:
	using buffer_t = std::vector<char>;
	using buffer_ptr = std::shared_ptr<buffer_t>;

	boost::asio::windows::stream_handle m_dev;
	tcp::socket m_sock;

	void async_read_header(buffer_ptr buf);
	void on_read_header(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf);

	void async_read_body(buffer_ptr buf);
	void on_read_body(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf);

	void async_write_pdu(buffer_ptr buf);
	void on_write_pdu(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf);
};


Forwarder::Forwarder(HANDLE hdev, SOCKET sockfd, int AddressFamily) :
	m_dev(get_io_context(), hdev),
	m_sock(get_io_context(), AddressFamily == AF_INET6 ? tcp::v6() : tcp::v4(), sockfd)
{
}

void Forwarder::async_start()
{
	async_read_header(std::make_shared<buffer_t>());
}

void Forwarder::async_read_header(buffer_ptr buf)
{
	if (terminated()) {
		return;
	}

	auto f = [self = shared_from_this(), buf] (auto&&... args)
	{
		self->on_read_header(std::forward<decltype(args)>(args)..., std::move(buf));
	};

	buf->resize(sizeof(usbip_header));
	async_read(m_dev, boost::asio::buffer(*buf), f);
}

void Forwarder::on_read_header(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu ", ec.value(), ec.message().c_str(), transferred);
		terminate();
	} else {
		assert(transferred == buf->size());
		async_read_body(std::move(buf));
	}
}

void Forwarder::async_read_body(buffer_ptr buf)
{
	if (terminated()) {
		return;
	}

	auto hdr = reinterpret_cast<usbip_header*>(buf->data());
	assert(buf->size() == sizeof(*hdr));

	if (auto len = get_pdu_payload_size(hdr)) {

		auto f = [self = shared_from_this(), buf] (auto&&... args)
		{
			self->on_read_body(std::forward<decltype(args)>(args)..., std::move(buf));
		};

		buf->resize(buf->size() + len);
		async_read(m_dev, boost::asio::buffer(*buf), f); // boost::asio::transfer_exactly(len), 
	} else {
		async_write_pdu(std::move(buf));
	}
}

void Forwarder::on_read_body(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu ", ec.value(), ec.message().c_str(), transferred);
		terminate();
	} else {
		assert(transferred == buf->size());
		async_write_pdu(std::move(buf));
	}
}

void Forwarder::async_write_pdu(buffer_ptr buf)
{
	if (terminated()) {
		return;
	}

	auto f = [self = shared_from_this(), buf] (auto&&... args)
	{
		self->on_write_pdu(std::forward<decltype(args)>(args)..., std::move(buf));
	};

	async_write(m_dev, boost::asio::buffer(*buf), f);
}

void Forwarder::on_write_pdu(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu", ec.value(), ec.message().c_str(), transferred);
		terminate();
	} else {
		assert(transferred == buf->size());
		async_read_header(std::move(buf));
	}
}

auto read(HANDLE hStdin, void *buf, DWORD len)
{
	for (DWORD offset = 0; offset < len; ) {

		DWORD nread{};

		if (ReadFile(hStdin, static_cast<char*>(buf) + offset, len - offset, &nread, nullptr) && nread) {
			offset += nread;
		} else {
			return false;
		}
	}

	return true;
}

auto read_data(HANDLE &hdev, SOCKET &sockfd, int &AddressFamily)
{
	auto hStdin = GetStdHandle(STD_INPUT_HANDLE);
	assert(hStdin != INVALID_HANDLE_VALUE);

	WSAPROTOCOL_INFOW ProtocolInfo{};
	auto ok = read(hStdin, &hdev, sizeof(hdev)) && read(hStdin, &ProtocolInfo, sizeof(ProtocolInfo));

	CloseHandle(hStdin);

	if (ok) {
		AddressFamily = ProtocolInfo.iAddressFamily;
		sockfd = WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &ProtocolInfo, 0, WSA_FLAG_OVERLAPPED);
		assert(sockfd != INVALID_SOCKET);
	}

	return hdev != INVALID_HANDLE_VALUE && sockfd != INVALID_SOCKET;
}

void run(HANDLE hdev, SOCKET sockfd, int AddressFamily)
{
	auto &ioc = get_io_context();

	boost::asio::signal_set signals(ioc, SIGTERM, SIGINT);
	signals.async_wait(signal_handler);

	std::make_shared<Forwarder>(hdev, sockfd, AddressFamily)->async_start();

	ioc.run();
	assert(terminated());
}

void setup_forwarder()
{
	HANDLE hdev = INVALID_HANDLE_VALUE;
	SOCKET sockfd = INVALID_SOCKET;
	int AddressFamily = AF_UNSPEC;

	if (read_data(hdev, sockfd, AddressFamily)) {
		run(hdev, sockfd, AddressFamily);
	}

	if (hdev != INVALID_HANDLE_VALUE) {
		DWORD unused{};
		DeviceIoControl(hdev, IOCTL_USBIP_VHCI_SHUTDOWN_HARDWARE, nullptr, 0, nullptr, 0, &unused, nullptr);
	}
}

} // namespace


int main()
{
	WPPInit wpp;

	try {
		setup_forwarder();
	} catch (std::exception &e) {
		Trace(TRACE_LEVEL_ERROR, "Exception: %s", e.what());
		g_exit_code = EXIT_FAILURE;
	}

	return g_exit_code;
}
