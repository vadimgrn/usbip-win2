#include "trace.h"
#include "attacher.tmh"

#include <cerrno>
#include <cassert>
#include <exception>
#include <atomic>
#include <vector>
#include <thread>

#include <boost/asio.hpp>

#include "usbip_proto.h"
#include "usbip_common.h"
#include "usbip_vhci_api.h"
#include "pdu.h"

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

auto& get_thread_pool()
{
	static std::thread v[2]; // read and write simultaneously
	return v;
}

auto exec(const std::function<void()> &func, const char *name)
{
	try {
		func();
		return false;
	} catch (std::exception &e) {
		Trace(TRACE_LEVEL_ERROR, "Exception(%s): %s", name, e.what());
	}

	return true;
}

void io_context_run(boost::asio::io_context &ioc)
{
	Trace(TRACE_LEVEL_INFORMATION, "Enter");

	auto run = [&ioc] { ioc.run(); };

	while (!ioc.stopped()) {
		if (exec(run, "io_context::run")) {
			terminate();
		}
	}

	Trace(TRACE_LEVEL_INFORMATION, "Leave");
}

void start_threads(boost::asio::io_context &ioc)
{
	for (auto &t : get_thread_pool()) {
		t = std::thread(io_context_run, std::ref(ioc));
	}
}

void stop_threads(boost::asio::io_context *ioc)
{
	Trace(TRACE_LEVEL_INFORMATION, "Enter");

	assert(ioc);
	ioc->stop();

	for (auto &t : get_thread_pool()) {
		if (t.joinable()) {
			auto join = [&t] { t.join(); };
			exec(join, "thread::join");
		}
	}

	Trace(TRACE_LEVEL_INFORMATION, "Leave");
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

	static auto &get_hdr(buffer_ptr buf) { return *reinterpret_cast<usbip_header*>(buf->data()); }

	void async_read_header(buffer_ptr buf, bool remote);
	void on_read_header(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);

	void async_read_body(buffer_ptr buf, bool remote);
	void on_read_body(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);

	void async_write_pdu(buffer_ptr buf, bool remote);
	void on_write_pdu(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);
};


Forwarder::Forwarder(HANDLE hdev, SOCKET sockfd, int AddressFamily) :
	m_dev(get_io_context(), hdev),
	m_sock(get_io_context(), AddressFamily == AF_INET6 ? tcp::v6() : tcp::v4(), sockfd)
{
}

void Forwarder::async_start()
{
	async_read_header(std::make_shared<buffer_t>(), false);
	async_read_header(std::make_shared<buffer_t>(), true);
}

void Forwarder::async_read_header(buffer_ptr buf, bool remote)
{
	if (terminated()) {
		return;
	}

	auto f = [self = shared_from_this(), buf, remote] (auto&&... args)
	{
		self->on_read_header(std::forward<decltype(args)>(args)..., std::move(buf), remote);
	};

	buf->resize(sizeof(usbip_header));
	auto hdr = boost::asio::buffer(*buf);
	
	remote ? async_read(m_sock, std::move(hdr), std::move(f)) : 
		 async_read(m_dev,  std::move(hdr), std::move(f));
}

void Forwarder::on_read_header(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu ", ec.value(), ec.message().c_str(), transferred);
		terminate();
	} else {
		assert(transferred == buf->size());
		Trace(TRACE_LEVEL_VERBOSE, "Remote %!bool!, transferred %Iu", remote, transferred);
		async_read_body(std::move(buf), remote);
	}
}

void Forwarder::async_read_body(buffer_ptr buf, bool remote)
{
	if (terminated()) {
		return;
	}

	auto &hdr = get_hdr(buf);
	assert(buf->size() == sizeof(hdr));

	if (remote) {
		byteswap_header(hdr, swap_dir::net2host);
	}

	if (auto len = get_payload_size(hdr)) {

		auto f = [self = shared_from_this(), buf, remote] (auto&&... args)
		{
			self->on_read_body(std::forward<decltype(args)>(args)..., std::move(buf), remote);
		};

		buf->resize(sizeof(hdr) + len);
		auto body = boost::asio::buffer(buf->data() + sizeof(hdr), len);

		remote ? async_read(m_sock, std::move(body), std::move(f)) : 
			 async_read(m_dev,  std::move(body), std::move(f));
	} else {
		async_write_pdu(std::move(buf), !remote);
	}
}

void Forwarder::on_read_body(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu ", ec.value(), ec.message().c_str(), transferred);
		terminate();
		return;
	}

	auto &hdr = get_hdr(buf);
	assert(transferred == buf->size() - sizeof(hdr));

	Trace(TRACE_LEVEL_VERBOSE, "Remote %!bool!, transferred %Iu", remote, transferred);

	byteswap_payload(hdr);
	async_write_pdu(std::move(buf), !remote);
}

void Forwarder::async_write_pdu(buffer_ptr buf, bool remote)
{
	if (terminated()) {
		return;
	}

	if (remote) {
		byteswap_header(get_hdr(buf), swap_dir::host2net);
	}

	auto f = [self = shared_from_this(), buf, remote] (auto&&... args)
	{
		self->on_write_pdu(std::forward<decltype(args)>(args)..., std::move(buf), remote);
	};

	auto data = boost::asio::buffer(*buf);

	remote ? async_write(m_sock, std::move(data), std::move(f)) :
		 async_write(m_dev,  std::move(data), std::move(f));
}

void Forwarder::on_write_pdu(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', transferred %Iu", ec.value(), ec.message().c_str(), transferred);
		terminate();
	} else {
		assert(transferred == buf->size());
		Trace(TRACE_LEVEL_VERBOSE, "Remote %!bool!, transferred %Iu", remote, transferred);
		async_read_header(std::move(buf), !remote);
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

auto start_thread_pool(boost::asio::io_context &ioc)
{
	std::unique_ptr<boost::asio::io_context, decltype(stop_threads)&> ptr(&ioc, stop_threads);
	start_threads(ioc);
	return ptr;
}

void run(HANDLE hdev, SOCKET sockfd, int AddressFamily)
{
	auto &ioc = get_io_context();

	boost::asio::signal_set signals(ioc, SIGTERM, SIGINT);
	signals.async_wait(signal_handler);

	std::make_shared<Forwarder>(hdev, sockfd, AddressFamily)->async_start();

	auto stop = start_thread_pool(ioc);

	ioc.run();
	assert(terminated());

	stop.reset();
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
		auto ok = DeviceIoControl(hdev, IOCTL_USBIP_VHCI_SHUTDOWN_HARDWARE, nullptr, 0, nullptr, 0, &unused, nullptr);
		if (!ok) {
			Trace(TRACE_LEVEL_ERROR, "VHCI_SHUTDOWN_HARDWARE error %#lu", GetLastError());
		}
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

	Trace(TRACE_LEVEL_INFORMATION, "Exiting %d", g_exit_code);
	return g_exit_code;
}
