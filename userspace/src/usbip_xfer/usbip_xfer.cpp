#include "usbip_xfer.h"
#include "trace.h"
#include "usbip_xfer.tmh"

#include <cerrno>
#include <cassert>
#include <exception>
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

int g_exit_code = EXIT_SUCCESS;

struct WPPInit
{
	WPPInit() noexcept { WPP_INIT_TRACING(nullptr); }
	~WPPInit() noexcept { WPP_CLEANUP(); }
};

auto &io_context()
{
	static boost::asio::io_context ctx(2); // for simultaneous read and write
	return ctx;
}

void terminate(int exit_code = EXIT_FAILURE)
{
	Trace(TRACE_LEVEL_INFORMATION, "Exit code %d", exit_code);

	*(volatile int*)&g_exit_code = exit_code;
	io_context().stop();
}

inline auto terminated()
{
	return io_context().stopped();
}

void signal_handler(const boost::system::error_code &ec, int signum)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "Error #%d '%s', signum %d", ec.value(), ec.message().c_str(), signum);
	} else {
		Trace(TRACE_LEVEL_INFORMATION, "signum %d", signum);
	}

	terminate(EXIT_SUCCESS);
}

auto try_catch(const std::function<void()> &func, const char *name) noexcept
{
	try {
		func();
		return false;
	} catch (std::exception &e) {
		Trace(TRACE_LEVEL_ERROR, "Exception(%s): %s", name, e.what());
	}

	return true;
}

void io_context_run(boost::asio::io_context &ioc, const char *caller)
{
	Trace(TRACE_LEVEL_INFORMATION, "%s enter", caller);

	auto run = [&ioc] { ioc.run(); };

	while (!ioc.stopped()) {
		if (try_catch(run, "io_context::run")) {
			terminate();
		}
	}

	Trace(TRACE_LEVEL_INFORMATION, "%s leave", caller);
}

/*
 * usbip_header_ret_submit always has zeros in usbip_header_basic's devid, direction, ep.
 * The most significant bit of seqnum is used to store transfer direction from a submit command.
 * @param hdr request to the server
 * See: <linux>/Documentation/usb/usbip_protocol.rst, usbip_header_basic. 
 */
inline void stash_submit_dir(usbip_header &hdr) noexcept
{
	auto &b = hdr.base;
	assert(b.command == USBIP_CMD_SUBMIT || b.command == USBIP_CMD_UNLINK);

	assert(!(b.seqnum & 0x8000'0000)); // vhci driver must never set the most significant bit of seqnum
	b.seqnum |= b.direction << 31;
}

static_assert(sizeof(usbip_header_basic::seqnum) == sizeof(UINT32));

/*
* @param hdr response from the server
*/
void restore_submit_dir(usbip_header &hdr) noexcept
{
	auto &b = hdr.base;
	assert(b.command == USBIP_RET_SUBMIT || b.command == USBIP_RET_UNLINK);

	assert(!b.direction);
	b.direction = b.seqnum >> 31; // vhci driver is relied on it

	b.seqnum &= 0x7FFF'FFFF; // clear the most significant bit
}


class Forwarder : public std::enable_shared_from_this<Forwarder>
{
public:
	Forwarder(HANDLE hdev, SOCKET sockfd, int AddressFamily);
	~Forwarder() { shutdown_hardware(); }

	void async_start();

private:
	using buffer_t = std::vector<char>;
	using buffer_ptr = std::shared_ptr<buffer_t>;
	int m_client = -1; // whoami

	boost::asio::windows::stream_handle m_dev;
	tcp::socket m_sock;
	//boost::asio::io_context::strand m_strand{io_context()};

	static auto &get_hdr(buffer_ptr buf) { return *reinterpret_cast<usbip_header*>(buf->data()); }
	static constexpr auto target(bool remote) noexcept { return remote ? "server" : "driver"; }

	void shutdown_hardware();
		
	void async_read_header(buffer_ptr buf, bool remote);
	void on_read_header(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);

	void async_read_body(buffer_ptr buf, bool remote);
	void on_read_body(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);

	void async_write_pdu(buffer_ptr buf, bool remote);
	void on_write_pdu(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);
};


Forwarder::Forwarder(HANDLE hdev, SOCKET sockfd, int AddressFamily) :
	m_dev(io_context(), hdev),
	m_sock(io_context(), AddressFamily == AF_INET6 ? tcp::v6() : tcp::v4(), sockfd)
{
}

void Forwarder::shutdown_hardware()
{
//	assert(m_dev.is_open());
	DWORD unused{};

	if (DeviceIoControl(m_dev.native_handle(), IOCTL_USBIP_VHCI_SHUTDOWN_HARDWARE, nullptr, 0, nullptr, 0, &unused, nullptr)) {
		Trace(TRACE_LEVEL_INFORMATION, "OK");
	} else {
		Trace(TRACE_LEVEL_ERROR, "Error %#lu", GetLastError());
	}
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

	if (m_client < 0) {
		m_client = !remote;
		Trace(TRACE_LEVEL_INFORMATION, "%s!", m_client ? "Client" : "Server");
	}

	auto f = [self = shared_from_this(), buf, remote] (auto&&... args)
	{
		self->on_read_header(std::forward<decltype(args)>(args)..., std::move(buf), remote);
	};

	buf->resize(sizeof(usbip_header));
	
	remote ? async_read(m_sock, boost::asio::buffer(*buf), std::move(f)) : // m_strand.wrap(f)
		 async_read(m_dev,  boost::asio::buffer(*buf), std::move(f));
}

void Forwarder::on_read_header(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "%s: error #%d '%s'", target(remote), ec.value(), ec.message().c_str());
		terminate();
	} else {
		Trace(TRACE_LEVEL_VERBOSE, "%s -> %Iu", target(remote), transferred);
		assert(transferred == buf->size());
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
		if (m_client) {
			restore_submit_dir(hdr);
		}
	} else if (m_client) {
		stash_submit_dir(hdr);
	}

	auto payload = get_payload_size(hdr);
	if (!payload) {
		async_write_pdu(std::move(buf), !remote);
		return;
	}

	auto f = [self = shared_from_this(), buf, remote] (auto&&... args)
	{
		self->on_read_body(std::forward<decltype(args)>(args)..., std::move(buf), remote);
	};

	buf->resize(sizeof(hdr) + payload);
	auto body = boost::asio::buffer(buf->data() + sizeof(hdr), payload);

	remote ? async_read(m_sock, std::move(body), std::move(f)) : 
		 async_read(m_dev,  std::move(body), std::move(f));
}

void Forwarder::on_read_body(
	const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "%s: error #%d '%s'", target(remote), ec.value(), ec.message().c_str());
		terminate();
		return;
	}

	Trace(TRACE_LEVEL_VERBOSE, "%s -> %Iu", target(remote), transferred);

	auto &hdr = get_hdr(buf);
	assert(transferred == buf->size() - sizeof(hdr));

	byteswap_payload(hdr);
	async_write_pdu(std::move(buf), !remote);
}

void Forwarder::async_write_pdu(buffer_ptr buf, bool remote)
{
	if (terminated()) {
		return;
	}

	auto &hdr = get_hdr(buf);

	{
		auto &b = hdr.base;
		Trace(TRACE_LEVEL_VERBOSE, "%s <- Hdr{%!usbip_request_type!, seqnum %#x, devid %#x, %s[%u]}",
						target(remote), b.command, b.seqnum, b.devid,
						b.direction == USBIP_DIR_OUT ? "out" : "in", b.ep);
	}

	if (remote) {
		byteswap_header(hdr, swap_dir::host2net);
	}

	auto f = [self = shared_from_this(), buf, remote] (auto&&... args)
	{
		self->on_write_pdu(std::forward<decltype(args)>(args)..., buf, remote);
	};

	remote ? async_write(m_sock, boost::asio::buffer(*buf), std::move(f)) :
		 async_write(m_dev,  boost::asio::buffer(*buf), std::move(f));
}

void Forwarder::on_write_pdu(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "%s: error #%d '%s'", target(remote), ec.value(), ec.message().c_str());
		terminate();
	} else {
		Trace(TRACE_LEVEL_VERBOSE, "%s <- %Iu", target(remote), transferred);
		assert(transferred == buf->size());
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

auto read_args(HANDLE &hdev, SOCKET &sockfd, int &AddressFamily)
{
	auto hStdin = GetStdHandle(STD_INPUT_HANDLE);
	assert(hStdin != INVALID_HANDLE_VALUE);

	usbip_xfer_args args{};
	auto ok = read(hStdin, &args, sizeof(args));

	CloseHandle(hStdin);

	if (ok) {
		hdev = args.hdev;
		assert(hdev != INVALID_HANDLE_VALUE);
		
		AddressFamily = args.info.iAddressFamily;

		sockfd = WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &args.info, 0, WSA_FLAG_OVERLAPPED);
		assert(sockfd != INVALID_SOCKET);
	}

	return hdev != INVALID_HANDLE_VALUE && sockfd != INVALID_SOCKET;
}

void join(std::thread *tr)
{
	assert(tr);
	assert(terminated());

	Trace(TRACE_LEVEL_INFORMATION, "Enter");

	if (tr->joinable()) {
		auto join = [tr] { tr->join(); };
		try_catch(join, "thread::join");
	}

	Trace(TRACE_LEVEL_INFORMATION, "Leave");
}

auto launch_thread()
{
	static std::thread tr(io_context_run, std::ref(io_context()), "thread");
	return std::unique_ptr<std::thread, decltype(join)&>(&tr, join);
}

void run(HANDLE hdev, SOCKET sockfd, int AddressFamily)
{
	auto &ioc = io_context();

	boost::asio::signal_set signals(ioc, SIGTERM, SIGINT);
	signals.async_wait(signal_handler);

	std::make_shared<Forwarder>(hdev, sockfd, AddressFamily)->async_start();

	if (auto join = launch_thread()) {
		io_context_run(ioc, "main"); // blocks thread
		assert(terminated());
		join.reset();
	}
}

void setup_forwarder()
{
	HANDLE hdev = INVALID_HANDLE_VALUE;
	SOCKET sockfd = INVALID_SOCKET;
	int AddressFamily = AF_UNSPEC;

	if (read_args(hdev, sockfd, AddressFamily)) {
		run(hdev, sockfd, AddressFamily);
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
