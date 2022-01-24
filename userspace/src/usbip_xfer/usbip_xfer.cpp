#include "usbip_xfer.h"
#include "trace.h"
#include "usbip_xfer.tmh"

#include <cerrno>
#include <cassert>
#include <exception>
#include <vector>
#include <thread>
#include <unordered_map>

#include <boost/asio.hpp>

#include "usbip_proto.h"
#include "usbip_common.h"
#include "usbip_vhci_api.h"
#include "pdu.h"
#include "debug.h"

namespace
{

using boost::asio::ip::tcp;

int g_exit_code = EXIT_SUCCESS;

struct WPPInit
{
	WPPInit() noexcept { WPP_INIT_TRACING(nullptr); }
	~WPPInit() noexcept { WPP_CLEANUP(); }
};

WPPInit wpp;

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

	using seqnums_type = std::unordered_map<seqnum_t, seqnum_t>;
	seqnums_type m_client_dir_in; // seqnum of cmd_submit -> seqnum of cmd_unlink, USBIP_DIR_IN client requests with data

	boost::asio::io_context::strand m_strand{io_context()};
	boost::asio::windows::stream_handle m_dev;
	tcp::socket m_sock;

	static auto &get_hdr(buffer_ptr buf) noexcept { return *reinterpret_cast<usbip_header*>(buf->data()); }
	static constexpr auto target(bool remote) noexcept { return remote ? "server" : "driver"; }

	void shutdown_hardware() noexcept;

	void save_client_request_dir(const usbip_header &req);
	void add_seqnums(seqnum_t seqnum, seqnum_t unlink_seqnum);
	void restore_server_response_dir(buffer_ptr resp);
	seqnums_type::iterator find_unlink_seqnum(seqnum_t seqnum);

	void async_read_header(buffer_ptr buf, bool remote);
	void on_read_header(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);

	void async_read_body(buffer_ptr buf, bool remote);
	void on_read_body(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);

	void async_write_pdu(buffer_ptr buf, bool remote);
	void on_write_pdu(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote);
};


Forwarder::Forwarder(HANDLE hdev, SOCKET sockfd, int AddressFamily) :
	m_dev(m_strand.context(), hdev),
	m_sock(m_strand.context(), AddressFamily == AF_INET6 ? tcp::v6() : tcp::v4(), sockfd)
{
}

void Forwarder::shutdown_hardware() noexcept
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

auto get_client_request_seqnums(const usbip_header &req) noexcept
{
	std::pair<seqnum_t, seqnum_t> res{};

	switch (req.base.command)
	{
	case USBIP_CMD_SUBMIT:
		if (req.base.direction == USBIP_DIR_IN && req.u.cmd_submit.transfer_buffer_length) {
			res.first = req.base.seqnum;
			assert(res.first);
		}
		break;
	case USBIP_CMD_UNLINK:
		res.first = req.base.seqnum;
		res.second = req.u.cmd_unlink.seqnum; // unknown if it is USBIP_DIR_IN, can't search m_client_dir_in here
		assert(res.first);
		assert(res.second);
		break;
	default:
		Trace(TRACE_LEVEL_CRITICAL, "%!usbip_request_type!: request expected", req.base.command);
		terminate();
	}

	return res;
}

void Forwarder::save_client_request_dir(const usbip_header &req)
{
	auto [seqnum, unlink_seqnum] = get_client_request_seqnums(req);
	if (!seqnum) {
		return;
	}

	auto f = [self = shared_from_this(), seqnum, unlink_seqnum] 
	{ 
		self->add_seqnums(seqnum, unlink_seqnum); 
	};

	dispatch(bind_executor(m_strand, std::move(f)));
}

void Forwarder::add_seqnums(seqnum_t seqnum, seqnum_t unlink_seqnum)
{
	if (!unlink_seqnum) {
		auto [i, was_inserted] = m_client_dir_in.emplace(seqnum, 0);
		assert(was_inserted);
		Trace(TRACE_LEVEL_VERBOSE, "Added seqnum %u, unlink_seqnum %u", seqnum, unlink_seqnum);
		return;
	}

	auto i = m_client_dir_in.find(unlink_seqnum);

	if (i != m_client_dir_in.end()) {
		assert(!i->second);
		i->second = seqnum;
		Trace(TRACE_LEVEL_VERBOSE, "Updated seqnum %u, unlink_seqnum %u", i->first, i->second);
	} else {
		Trace(TRACE_LEVEL_VERBOSE, "Skip seqnum %u, unlink_seqnum %u is not USBIP_DIR_IN request with data", 
						seqnum, unlink_seqnum);
	}
}

auto Forwarder::find_unlink_seqnum(seqnum_t seqnum) -> seqnums_type::iterator
{
	auto res = m_client_dir_in.end();

	for (auto i = m_client_dir_in.begin(); i != m_client_dir_in.end(); ++i) {
		if (i->second == seqnum) {
			res = i;
			break;
		}
	}

	return res;
}

/*
 * usbip_header_ret_submit always has zeros in usbip_header_basic's devid, direction, ep.
 * After a successful USBIP_RET_UNLINK (status = -ECONNRESET), the unlinked URB submission
 * would not have a corresponding USBIP_RET_SUBMIT. 
 * If USBIP_RET_UNLINK status is zero, USBIP_RET_SUBMIT response was already sent.
 * See: <linux>/Documentation/usb/usbip_protocol.rst
 */
void Forwarder::restore_server_response_dir(buffer_ptr resp)
{
	enum { ECONNRESET_LNX = 104 }; // <linux>/include/uapi/asm-generic/errno.h

	auto &hdr = get_hdr(resp);
	assert(!hdr.base.direction); // always zero for server responses

	auto seqnum = hdr.base.seqnum;

	switch (auto cmd = hdr.base.command)
	{
	case USBIP_RET_SUBMIT: {
		auto i = m_client_dir_in.find(seqnum);
		if (i != m_client_dir_in.end()) {
			Trace(TRACE_LEVEL_VERBOSE, "%!usbip_request_type!: remove seqnum %u, unlink_seqnum %u", 
						cmd, i->first, i->second);

			hdr.base.direction = USBIP_DIR_IN;
			m_client_dir_in.erase(i);
		}
	} 
		break;
	case USBIP_RET_UNLINK: {
		auto i = find_unlink_seqnum(seqnum);
		switch (hdr.u.ret_unlink.status) 
		{
		case 0: // too late, RET_SUBMIT was already received
			assert(i == m_client_dir_in.end());
			break;
		case -ECONNRESET_LNX: // unlinked, RET_SUBMIT won't be issued
			if (i != m_client_dir_in.end()) {
				Trace(TRACE_LEVEL_VERBOSE, "%!usbip_request_type!: remove seqnum %u, unlink_seqnum %u",
							cmd, i->first, i->second);

				m_client_dir_in.erase(i);
			}
			break;
		}
	}
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "%!usbip_request_type!: response expected", cmd);
		terminate();
		return;
	}

	auto f = [self = shared_from_this(), buf = std::move(resp)] 
	{ 
		self->async_read_body(std::move(buf), true); // now get_payload_size will return correct result for DIR_IN
	};

	boost::asio::post(std::move(f));
}

void Forwarder::async_read_header(buffer_ptr buf, bool remote)
{
	if (terminated()) {
		return;
	}

	if (m_client < 0) { // once
		m_client = !remote;
		Trace(TRACE_LEVEL_INFORMATION, "%s!", m_client ? "Client" : "Server");
	}

	auto f = [self = shared_from_this(), buf, remote] (auto&&... args)
	{
		self->on_read_header(std::forward<decltype(args)>(args)..., std::move(buf), remote);
	};

	buf->resize(sizeof(usbip_header));
	
	remote ? async_read(m_sock, boost::asio::buffer(*buf), std::move(f)) :
		 async_read(m_dev,  boost::asio::buffer(*buf), std::move(f));
}

void Forwarder::on_read_header(const boost::system::error_code &ec, std::size_t transferred, buffer_ptr buf, bool remote)
{
	if (ec) {
		Trace(TRACE_LEVEL_ERROR, "%s <- error #%d '%s'", target(remote), ec.value(), ec.message().c_str());
		terminate();
		return;
	}

	Trace(TRACE_LEVEL_VERBOSE, "%s -> %Iu", target(remote), transferred);
	assert(transferred == buf->size());

	auto &hdr = get_hdr(buf);

	if (!remote) { // reading from the driver's device handle
		if (m_client) {
			save_client_request_dir(hdr);
		}
		async_read_body(std::move(buf), remote);
		return;
	}

	byteswap_header(hdr, swap_dir::net2host);

	if (!m_client) {
		async_read_body(std::move(buf), remote);
		return;
	}

	auto f = [self = shared_from_this(), buf = std::move(buf)] 
	{ 
		self->restore_server_response_dir(std::move(buf)); 
	};

	dispatch(bind_executor(m_strand, std::move(f)));
}

void Forwarder::async_read_body(buffer_ptr buf, bool remote)
{
	if (terminated()) {
		return;
	}

	auto &hdr = get_hdr(buf);
	assert(buf->size() == sizeof(hdr));

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
		Trace(TRACE_LEVEL_ERROR, "%s <- error #%d '%s'", target(remote), ec.value(), ec.message().c_str());
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
		char str[PRINT_BUFSZ];
		Trace(TRACE_LEVEL_INFORMATION, "%s <- %Iu%s", target(remote), buf->size(), print(str, sizeof(str), hdr));
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
		Trace(TRACE_LEVEL_ERROR, "%s <- error #%d '%s'", target(remote), ec.value(), ec.message().c_str());
		terminate();
	} else {
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
	try {
		setup_forwarder();
	} catch (std::exception &e) {
		Trace(TRACE_LEVEL_ERROR, "Exception: %s", e.what());
		g_exit_code = EXIT_FAILURE;
	}

	Trace(TRACE_LEVEL_INFORMATION, "Exit code %d", g_exit_code);
	return g_exit_code;
}
