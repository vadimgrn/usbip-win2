//#define DEBUG_PDU
#include "usbip_windows.h"

#include <signal.h>
#include <stdlib.h>

#include "usbip_proto.h"
#include "usbip_network.h"

#define BUFREAD_P(devbuf)	((devbuf)->offp - (devbuf)->offhdr)
#define BUFREADMAX_P(devbuf)	((devbuf)->bufmaxp - (devbuf)->offp)
#define BUFREMAIN_C(devbuf)	((devbuf)->bufmaxc - (devbuf)->offc)
#define BUFHDR_P(devbuf)	((devbuf)->bufp + (devbuf)->offhdr)
#define BUFCUR_P(devbuf)	((devbuf)->bufp + (devbuf)->offp)
#define BUFCUR_C(devbuf)	((devbuf)->bufc + (devbuf)->offc)

typedef struct _devbuf {
	const char	*desc;
	BOOL	is_req, swap_req;
	BOOL	invalid;
	/* asynchronous read is in progress */
	BOOL	in_reading;
	/* asynchronous write is in progress */
	BOOL	in_writing;
	/* step 1: reading header, 2: reading data */
	int	step_reading;
	HANDLE	hdev;
	char	*bufp, *bufc;	/* bufp: producer, bufc: consumer */
	DWORD	offhdr;		/* header offset for producer */
	DWORD	offp, offc;	/* offp: producer offset, offc: consumer offset */
	DWORD	bufmaxp, bufmaxc;
	struct _devbuf	*peer;
	OVERLAPPED	ovs[2];
	/* completion event for read or write */
	HANDLE	hEvent;
} devbuf_t;

/*
 * Two devbuf's are shared via hEvent, which indicates read or write completion.
 * Such a global variable does not pose a severe limitation.
 * Because userspace binaries(usbip.exe, usbipd.exe) have only a single usbip_forward().
 */
static HANDLE	hEvent;

#ifdef DEBUG_PDU
#undef USING_STDOUT

static void
dbg_to_file(char *fmt, ...)
{
	FILE	*fp;
	va_list ap;

#ifdef USING_STDOUT
	fp = stdout;
#else
	if (fopen_s(&fp, "debug_pdu.log", "a+") != 0)
		return;
#endif
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
#ifndef USING_STDOUT
	fclose(fp);
#endif
}

static const char *
dbg_usbip_hdr_cmd(unsigned int cmd)
{
	switch (cmd) {
	case USBIP_CMD_SUBMIT:
		return "CMD_SUBMIT";
	case USBIP_RET_SUBMIT:
		return "RET_SUBMIT";
	case USBIP_CMD_UNLINK:
		return "CMD_UNLINK";
	case USBIP_RET_UNLINK:
		return "RET_UNLINK";
	default:
		return "UNKNOWN";
	}
}

static void
dump_iso_pkts(struct usbip_header *hdr)
{
	struct usbip_iso_packet_descriptor	*iso_desc;
	int	n_pkts;
	int	i;

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		n_pkts = hdr->u.cmd_submit.number_of_packets;
		if (hdr->base.direction)
			iso_desc = (struct usbip_iso_packet_descriptor *)(hdr + 1);
		else
			iso_desc = (struct usbip_iso_packet_descriptor *)((char *)(hdr + 1) + hdr->u.cmd_submit.transfer_buffer_length);
		break;
	case USBIP_RET_SUBMIT:
		n_pkts = hdr->u.ret_submit.number_of_packets;
		if (hdr->base.direction)
			iso_desc = (struct usbip_iso_packet_descriptor *)((char *)(hdr + 1) + hdr->u.ret_submit.actual_length);
		else
			iso_desc = (struct usbip_iso_packet_descriptor *)(hdr + 1);
		break;
	default:
		return;
	}

	for (i = 0; i < n_pkts; i++) {
		dbg_to_file("  o:%d,l:%d,al:%d,st:%d\n", iso_desc->offset, iso_desc->length, iso_desc->actual_length, iso_desc->status);
		iso_desc++;
	}
}

static void
dump_usbip_header(struct usbip_header *hdr)
{
	dbg_to_file("DUMP: %s,seq:%u,devid:%x,dir:%s,ep:%x\n",
		dbg_usbip_hdr_cmd(hdr->base.command), hdr->base.seqnum, hdr->base.devid, hdr->base.direction ? "in": "out", hdr->base.ep);

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		dbg_to_file("  flags:%x,len:%x,sf:%x,#p:%x,intv:%x\n",
			hdr->u.cmd_submit.transfer_flags,
			hdr->u.cmd_submit.transfer_buffer_length,
			hdr->u.cmd_submit.start_frame,
			hdr->u.cmd_submit.number_of_packets,
			hdr->u.cmd_submit.interval);
		dbg_to_file("  setup: %02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx\n",
			hdr->u.cmd_submit.setup[0], hdr->u.cmd_submit.setup[1], hdr->u.cmd_submit.setup[2],
			hdr->u.cmd_submit.setup[3], hdr->u.cmd_submit.setup[4], hdr->u.cmd_submit.setup[5],
			hdr->u.cmd_submit.setup[6], hdr->u.cmd_submit.setup[7]);
		dump_iso_pkts(hdr);
		break;
	case USBIP_CMD_UNLINK:
		dbg_to_file("  seq:%x\n", hdr->u.cmd_unlink.seqnum);
		break;
	case USBIP_RET_SUBMIT:
		dbg_to_file("  st:%d,al:%d,sf:%d,#p:%d,ec:%d\n",
			hdr->u.ret_submit.status,
			hdr->u.ret_submit.actual_length,
			hdr->u.ret_submit.start_frame,
			hdr->u.cmd_submit.number_of_packets,
			hdr->u.ret_submit.error_count);
		dump_iso_pkts(hdr);
		break;
	case USBIP_RET_UNLINK:
		dbg_to_file(" st:%d\n", hdr->u.ret_unlink.status);
		break;
	default:
		/* NOT REACHED */
		break;
	}
	dbg_to_file("DUMP DONE-------\n");
}

#define DBGF(fmt, ...)		dbg_to_file(fmt, ## __VA_ARGS__)
#define DBG_USBIP_HEADER(hdr)	dump_usbip_header(hdr)

#else

#define DBGF(fmt, ...)
#define DBG_USBIP_HEADER(hdr)

#endif

static void
swap_usbip_header_base_endian(struct usbip_header_basic *base)
{
	base->command	= htonl(base->command);
	base->seqnum	= htonl(base->seqnum);
	base->devid	= htonl(base->devid);
	base->direction	= htonl(base->direction);
	base->ep	= htonl(base->ep);
}

static void
swap_cmd_submit_endian(struct usbip_header_cmd_submit *pdu)
{
	pdu->transfer_flags	= ntohl(pdu->transfer_flags);
	pdu->transfer_buffer_length = ntohl(pdu->transfer_buffer_length);
	pdu->start_frame = ntohl(pdu->start_frame);
	pdu->number_of_packets = ntohl(pdu->number_of_packets);
	pdu->interval = ntohl(pdu->interval);
}

static void
swap_ret_submit_endian(struct usbip_header_ret_submit *pdu)
{
	pdu->status = ntohl(pdu->status);
	pdu->actual_length = ntohl(pdu->actual_length);
	pdu->start_frame = ntohl(pdu->start_frame);
	pdu->number_of_packets = ntohl(pdu->number_of_packets);
	pdu->error_count = ntohl(pdu->error_count);
}

static void
swap_cmd_unlink_endian(struct usbip_header_cmd_unlink *pdu)
{
	pdu->seqnum = ntohl(pdu->seqnum);
}

static void
swap_ret_unlink_endian(struct usbip_header_ret_unlink *pdu)
{
	pdu->status = ntohl(pdu->status);
}

static void
swap_usbip_header_cmd(unsigned int cmd, struct usbip_header *hdr)
{
	switch (cmd) {
	case USBIP_CMD_SUBMIT:
		swap_cmd_submit_endian(&hdr->u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		swap_ret_submit_endian(&hdr->u.ret_submit);
		break;
	case USBIP_CMD_UNLINK:
		swap_cmd_unlink_endian(&hdr->u.cmd_unlink);
		break;
	case USBIP_RET_UNLINK:
		swap_ret_unlink_endian(&hdr->u.ret_unlink);
		break;
	default:
		/* NOTREACHED */
		dbg("unknown command in pdu header: %d", cmd);
		break;
	}
}

static void
swap_usbip_header_endian(struct usbip_header *hdr, BOOL from_swapped)
{
	unsigned int	cmd;

	if (from_swapped) {
		swap_usbip_header_base_endian(&hdr->base);
		cmd = hdr->base.command;
	}
	else {
		cmd = hdr->base.command;
		swap_usbip_header_base_endian(&hdr->base);
	}
	swap_usbip_header_cmd(cmd, hdr);
}

static void
swap_iso_descs_endian(char *buf, int num)
{
	struct usbip_iso_packet_descriptor	*ip_desc;
	int i;

	ip_desc = (struct usbip_iso_packet_descriptor *)buf;
	for (i = 0; i < num; i++) {
		ip_desc->offset = ntohl(ip_desc->offset);
		ip_desc->status = ntohl(ip_desc->status);
		ip_desc->length = ntohl(ip_desc->length);
		ip_desc->actual_length = ntohl(ip_desc->actual_length);
		ip_desc++;
	}
}

/*
 * This is a 'usbip_header_basic' cache to hold transfer direction of all
 * OUT CMD_SUBMIT packets. Cache info is used when RET_SUBMIT packets with the
 * same sequence number arrive.
 * The transfer direction, EP address and device ID are not provided in return
 * RET_SUBMIT packets from Linux, when used as an USBIP server.
 *
 * The transfer direction is needed to determine USBIP_RET_SUBMIT packet size
 * in this example:
 * The OUT ISOCHRONOUS transfer sends data buffer and its ISO descriptors towards
 * the device in CMD_SUBMIT packets with 'actual_size' record set to define the
 * data buffer size. However the return RET_SUBMIT packet of the same OUT transfer
 * contain only ISO descriptor and the 'actual_size' is set to the sent size value.
 *
 * Each cache entry may be written or read many times, however the sequence
 * number of a cache entry location is kept until current sequence number of
 * packets increases its value for the number of all cache entries.
 */
struct usbip_cached_hdr {
	UINT32 seqnum;
	// UINT32 devid;
	UINT32 direction;
	// UINT32 ep;
};

#define HDRS_CACHE_SIZE 1024
static struct usbip_cached_hdr hdrs_cache[HDRS_CACHE_SIZE];

static inline void
hdrs_cache_insert(struct usbip_header *usbip_hdr)
{
	int	idx = usbip_hdr->base.seqnum % HDRS_CACHE_SIZE;

	hdrs_cache[idx].seqnum = usbip_hdr->base.seqnum;
	hdrs_cache[idx].direction = usbip_hdr->base.direction;
}

static inline UINT32
hdrs_cache_direction(struct usbip_header *usbip_hdr)
{
	int	idx = usbip_hdr->base.seqnum % HDRS_CACHE_SIZE;

	if (usbip_hdr->base.seqnum == hdrs_cache[idx].seqnum) {
		/* Restore packet direction! */
		usbip_hdr->base.direction = hdrs_cache[idx].direction;
		return hdrs_cache[idx].direction;
	}
	/*
	 * If not in cache, return what is in the header!
	 */
	return usbip_hdr->base.direction;
}

static int
get_xfer_len(BOOL is_req, struct usbip_header *hdr)
{
	if (is_req) {
		if (hdr->base.command == USBIP_CMD_UNLINK)
			return 0;
		hdrs_cache_insert(hdr);
		if (hdr->base.direction)
			return 0;
		return hdr->u.cmd_submit.transfer_buffer_length;
	}
	else {
		if (hdr->base.command == USBIP_RET_UNLINK)
			return 0;
		if (hdrs_cache_direction(hdr) == USBIP_DIR_OUT)
			return 0;
		return hdr->u.ret_submit.actual_length;
	}
}

static int
get_iso_len(BOOL is_req, struct usbip_header *hdr)
{
	if (is_req) {
		if (hdr->base.command == USBIP_CMD_UNLINK)
			return 0;
		return hdr->u.cmd_submit.number_of_packets * sizeof(struct usbip_iso_packet_descriptor);
	}
	else {
		if (hdr->base.command == USBIP_RET_UNLINK)
			return 0;
		return hdr->u.ret_submit.number_of_packets * sizeof(struct usbip_iso_packet_descriptor);
	}
}

static BOOL
setup_rw_overlapped(devbuf_t *buff)
{
	int	i;

	for (i = 0; i < 2; i++) {
		memset(&buff->ovs[i], 0, sizeof(OVERLAPPED));
		buff->ovs[i].hEvent = (HANDLE)buff;
	}
	return TRUE;
}

static BOOL
init_devbuf(devbuf_t *buff, const char *desc, BOOL is_req, BOOL swap_req, HANDLE hdev, HANDLE hEvent)
{
	buff->bufp = (char *)malloc(1024);
	if (buff->bufp == NULL)
		return FALSE;
	buff->bufc = buff->bufp;
	buff->desc = desc;
	buff->is_req = is_req;
	buff->swap_req = swap_req;
	buff->in_reading = FALSE;
	buff->in_writing = FALSE;
	buff->invalid = FALSE;
	buff->step_reading = 0;
	buff->offhdr = 0;
	buff->offp = 0;
	buff->offc = 0;
	buff->bufmaxp = 1024;
	buff->bufmaxc = 0;
	buff->hdev = hdev;
	buff->hEvent = hEvent;
	if (!setup_rw_overlapped(buff)) {
		free(buff->bufp);
		return FALSE;
	}
	return TRUE;
}

static void
cleanup_devbuf(devbuf_t *buff)
{
	free(buff->bufp);
	if (buff->bufp != buff->bufc)
		free(buff->bufc);
}

static VOID CALLBACK
read_completion(DWORD errcode, DWORD nread, LPOVERLAPPED lpOverlapped)
{
	devbuf_t	*rbuff;

	rbuff = (devbuf_t *)lpOverlapped->hEvent;
	if (errcode == 0) {
		rbuff->offp += nread;
		if (nread == 0)
			rbuff->invalid = TRUE;
	}
	else if (errcode == ERROR_DEVICE_NOT_CONNECTED) {
		rbuff->invalid = TRUE;
	}
	rbuff->in_reading = FALSE;
	SetEvent(rbuff->hEvent);
}

static BOOL
read_devbuf(devbuf_t *rbuff, DWORD nreq)
{
	if (BUFREADMAX_P(rbuff) < nreq) {
		char	*bufnew;

		if (rbuff->bufp != rbuff->bufc) {
			/* reallocation is allowed only if producer and consumer use their own buffers */
			DWORD	nmore = nreq - BUFREADMAX_P(rbuff);

			bufnew = (char *)realloc(rbuff->bufp, rbuff->bufmaxp + nmore);
			if (bufnew == NULL) {
				dbg("failed to reallocate buffer: %s", rbuff->desc);
				return FALSE;
			}
			rbuff->bufp = bufnew;
			rbuff->bufmaxp += nmore;
		}
		else {
			DWORD	nexist = BUFREAD_P(rbuff);

			bufnew = (char *)malloc(nreq + nexist);
			if (bufnew == NULL) {
				dbg("failed to allocate buffer: %s", rbuff->desc);
				return FALSE;
			}
			if (nexist > 0) {
				/* copy from already read usbip header */
				memcpy(bufnew, BUFHDR_P(rbuff), nexist);
			}
			rbuff->bufp = bufnew;
			rbuff->offhdr = 0;
			rbuff->offp = nexist;
			rbuff->bufmaxp = nreq + nexist;
		}
	}

	if (!rbuff->in_reading) {
		if (!ReadFileEx(rbuff->hdev, BUFCUR_P(rbuff), nreq, &rbuff->ovs[0], read_completion)) {
			DWORD error = GetLastError();
			dbg("failed to read: err: 0x%lx", error);
			if (error == ERROR_NETNAME_DELETED) {
				dbg("could the client have dropped the connection?");
			}
			return FALSE;
		}
		rbuff->in_reading = TRUE;
	}
	return TRUE;
}

static VOID CALLBACK
write_completion(DWORD errcode, DWORD nwrite, LPOVERLAPPED lpOverlapped)
{
	devbuf_t	*wbuff, *rbuff;

	wbuff = (devbuf_t*)lpOverlapped->hEvent;
	wbuff->in_writing = FALSE;

	SetEvent(wbuff->hEvent);

	if (errcode != 0)
		return;

	if (nwrite == 0) {
		wbuff->invalid = TRUE;
		return;
	}
	rbuff = wbuff->peer;
	rbuff->offc += nwrite;
}

static BOOL
write_devbuf(devbuf_t *wbuff, devbuf_t *rbuff)
{
	if (rbuff->bufp != rbuff->bufc && BUFREMAIN_C(rbuff) == 0) {
		free(rbuff->bufc);
		rbuff->bufc = rbuff->bufp;
		rbuff->offc = 0;
		rbuff->bufmaxc = rbuff->offhdr;
	}
	if (!wbuff->in_writing && BUFREMAIN_C(rbuff) > 0) {
		if (!WriteFileEx(wbuff->hdev, BUFCUR_C(rbuff), BUFREMAIN_C(rbuff), &wbuff->ovs[1], write_completion)) {
			dbg("failed to write sock: err: 0x%lx", GetLastError());
			return FALSE;
		}
		wbuff->in_writing = TRUE;
	}

	return TRUE;
}

static int
read_dev(devbuf_t *rbuff, BOOL swap_req_write)
{
	struct usbip_header	*hdr;
	unsigned long	xfer_len, iso_len, len_data;

	if (BUFREAD_P(rbuff) < sizeof(struct usbip_header)) {
		rbuff->step_reading = 1;
		if (!read_devbuf(rbuff, sizeof(struct usbip_header) - BUFREAD_P(rbuff)))
			return -1;
		return 0;
	}

	hdr = (struct usbip_header *)BUFHDR_P(rbuff);
	if (rbuff->step_reading == 1) {
		if (rbuff->swap_req)
			swap_usbip_header_endian(hdr, TRUE);
		rbuff->step_reading = 2;
	}

	xfer_len = get_xfer_len(rbuff->is_req, hdr);
	iso_len = get_iso_len(rbuff->is_req, hdr);

	len_data = xfer_len + iso_len;
	if (BUFREAD_P(rbuff) < len_data + sizeof(struct usbip_header)) {
		DWORD	nmore = (DWORD)(len_data + sizeof(struct usbip_header)) - BUFREAD_P(rbuff);

		if (!read_devbuf(rbuff, nmore))
			return -1;
		return 0;
	}

	if (rbuff->swap_req && iso_len > 0)
		swap_iso_descs_endian((char *)(hdr + 1) + xfer_len, hdr->u.ret_submit.number_of_packets);

	DBG_USBIP_HEADER(hdr);

	if (swap_req_write) {
		if (iso_len > 0)
			swap_iso_descs_endian((char *)(hdr + 1) + xfer_len, hdr->u.ret_submit.number_of_packets);
		swap_usbip_header_endian(hdr, FALSE);
	}

	rbuff->offhdr += (sizeof(struct usbip_header) + len_data);
	if (rbuff->bufp == rbuff->bufc)
		rbuff->bufmaxc = rbuff->offp;
	rbuff->step_reading = 0;

	return 1;
}

static BOOL
read_write_dev(devbuf_t *rbuff, devbuf_t *wbuff)
{
	int	res;

	if (!rbuff->in_reading) {
		if ((res = read_dev(rbuff, wbuff->swap_req)) < 0)
			return FALSE;
		if (res == 0)
			return TRUE;
	}
	return write_devbuf(wbuff, rbuff);
}

static volatile BOOL	interrupted;

static void
signalhandler(int signal)
{
	interrupted = TRUE;
	SetEvent(hEvent);
}

void
usbip_forward(HANDLE hdev_src, HANDLE hdev_dst, BOOL inbound)
{
	devbuf_t	buff_src, buff_dst;
	const char* desc_src, * desc_dst;
	BOOL	is_req_src;
	BOOL	swap_req_src, swap_req_dst;

	if (inbound) {
		desc_src = "socket";
		desc_dst = "stub";
		is_req_src = TRUE;
		swap_req_src = TRUE;
		swap_req_dst = FALSE;
	}
	else {
		desc_src = "vhci";
		desc_dst = "socket";
		is_req_src = FALSE;
		swap_req_src = FALSE;
		swap_req_dst = TRUE;
	}

	hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (hEvent == NULL) {
		dbg("failed to create event");
		return;
	}

	if (!init_devbuf(&buff_src, desc_src, TRUE, swap_req_src, hdev_src, hEvent)) {
		CloseHandle(hEvent);
		dbg("failed to initialize %s buffer", desc_src);
		return;
	}
	if (!init_devbuf(&buff_dst, desc_dst, FALSE, swap_req_dst, hdev_dst, hEvent)) {
		CloseHandle(hEvent);
		dbg("failed to initialize %s buffer", desc_dst);
		cleanup_devbuf(&buff_src);
		return;
	}

	buff_src.peer = &buff_dst;
	buff_dst.peer = &buff_src;

	signal(SIGINT, signalhandler);

	while (!interrupted) {
		if (!read_write_dev(&buff_src, &buff_dst))
			break;
		if (!read_write_dev(&buff_dst, &buff_src))
			break;

		if (buff_src.invalid || buff_dst.invalid)
			break;
		if (buff_src.in_reading && buff_dst.in_reading &&
			(buff_src.in_writing || BUFREMAIN_C(&buff_dst) == 0) &&
			(buff_dst.in_writing || BUFREMAIN_C(&buff_src) == 0)) {
			WaitForSingleObjectEx(hEvent, INFINITE, TRUE);
			ResetEvent(hEvent);
		}
	}

	if (interrupted) {
		info("CTRL-C received\n");
	}
	signal(SIGINT, SIG_DFL);

	if (buff_src.in_reading)
		CancelIoEx(hdev_src, &buff_src.ovs[0]);
	if (buff_dst.in_reading)
		CancelIoEx(hdev_dst, &buff_dst.ovs[0]);

	while (buff_src.in_reading || buff_dst.in_reading || buff_src.in_writing || buff_dst.in_writing) {
		WaitForSingleObjectEx(hEvent, INFINITE, TRUE);
	}

	cleanup_devbuf(&buff_src);
	cleanup_devbuf(&buff_dst);
	CloseHandle(hEvent);
}
