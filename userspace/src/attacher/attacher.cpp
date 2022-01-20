#include "trace.h"
#include "attacher.tmh"

#include <cerrno>
#include <cassert>
#include <exception>
#include <atomic>
#include <vector>
#include <array>
#include <string>

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

inline auto terminated()
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
	Forwarder(HANDLE hdev, SOCKET sockfd);

	void run();

private:
	using header_buffer = std::array<char, sizeof(usbip_header)>;
	using header_ptr = std::shared_ptr<header_buffer>;

	using data_buffer = std::vector<char>;
	using data_ptr = std::shared_ptr<data_buffer>;

	boost::asio::windows::stream_handle m_dev;
	tcp::socket m_sock;

	void sched_dev_hdr_read(header_ptr hdr, data_ptr data);

	void on_dev_hdr_read (const boost::system::error_code &ec, std::size_t transferred, header_ptr hdr, data_ptr data);
	void on_dev_data_read(const boost::system::error_code &ec, std::size_t transferred, header_ptr hdr, data_ptr data);

	void sched_socket_write(header_ptr hdr, data_ptr data);
	void on_socket_write(const boost::system::error_code &ec, std::size_t transferred, header_ptr hdr, data_ptr data);
};


Forwarder::Forwarder(HANDLE hdev, SOCKET sockfd) :
	m_dev(get_io_context(), hdev),
	m_sock(get_io_context(), tcp::v4(), sockfd)
{
}

void Forwarder::run()
{
	sched_dev_hdr_read(std::make_shared<header_buffer>(), std::make_shared<data_buffer>());
}

void Forwarder::sched_dev_hdr_read(header_ptr hdr, data_ptr data)
{
	if (terminated()) {
		return;
	}

	auto f = [self = shared_from_this(), hdr, data = std::move(data)] (auto&&... args)
	{
		self->on_dev_hdr_read(std::forward<decltype(args)>(args)..., std::move(hdr), std::move(data));
	};

	async_read(m_dev, boost::asio::buffer(*hdr), f);
}

void Forwarder::on_dev_hdr_read(const boost::system::error_code &ec, std::size_t transferred, header_ptr hdr, data_ptr data)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu ", ec.value(), ec.message().c_str(), transferred);
		terminate();
		return;
	} 
	
	assert(transferred == hdr->size());

	if (terminated()) {
		return;
	}

	auto head = reinterpret_cast<usbip_header*>(hdr->data());

	auto len = get_pdu_payload_size(head);
	data->resize(len);

	if (len) {
		auto f = [self = shared_from_this(), hdr = std::move(hdr), data] (auto&&... args)
		{
			self->on_dev_data_read(std::forward<decltype(args)>(args)..., std::move(hdr), std::move(data));
		};

		async_read(m_dev, boost::asio::buffer(*data), f); // boost::asio::transfer_exactly(len), 
	} else {
		sched_socket_write(std::move(hdr), std::move(data));
	}
}

void Forwarder::on_dev_data_read(const boost::system::error_code &ec, std::size_t transferred, header_ptr hdr, data_ptr data)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu ", ec.value(), ec.message().c_str(), transferred);
		terminate();
	} else {
		assert(transferred == data->size());
		sched_socket_write(std::move(hdr), std::move(data));
	}
}

void Forwarder::sched_socket_write(header_ptr hdr, data_ptr data)
{
	if (terminated()) {
		return;
	}

	auto f = [self = shared_from_this(), hdr, data] (auto&&... args)
	{
		self->on_socket_write(std::forward<decltype(args)>(args)..., std::move(hdr), std::move(data));
	};

	std::array<boost::asio::const_buffer, 2> buffers = {
		boost::asio::buffer(*hdr),
		boost::asio::buffer(*data)
	};

	async_write(m_dev, buffers, f);
}

void Forwarder::on_socket_write(const boost::system::error_code &ec, std::size_t transferred, header_ptr hdr, data_ptr data)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu", ec.value(), ec.message().c_str(), transferred);
		terminate();
	} else {
		assert(transferred == hdr->size() + data->size());
		sched_dev_hdr_read(std::move(hdr), std::move(data));
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

auto read_data(HANDLE &hdev, SOCKET &sockfd)
{
	auto hStdin = GetStdHandle(STD_INPUT_HANDLE);
	assert(hStdin != INVALID_HANDLE_VALUE);

	WSAPROTOCOL_INFOW ProtocolInfo{};
	auto ok = read(hStdin, &hdev, sizeof(hdev)) && read(hStdin, &ProtocolInfo, sizeof(ProtocolInfo));

	CloseHandle(hStdin);

	if (ok) {
		sockfd = WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &ProtocolInfo, 0, WSA_FLAG_OVERLAPPED);
		assert(sockfd != INVALID_SOCKET);
	}

	return hdev != INVALID_HANDLE_VALUE && sockfd != INVALID_SOCKET;
}

void run(HANDLE hdev, SOCKET sockfd)
{
	auto &ctx = get_io_context();

	boost::asio::signal_set signals(ctx, SIGTERM, SIGINT);
	signals.async_wait(signal_handler);

	std::make_shared<Forwarder>(hdev, sockfd)->run();

	ctx.run();
	assert(terminated());
}

void setup_forwarder()
{
	HANDLE hdev = INVALID_HANDLE_VALUE;
	SOCKET sockfd = INVALID_SOCKET;

	if (read_data(hdev, sockfd)) {
		run(hdev, sockfd);
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
