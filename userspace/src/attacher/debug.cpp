#include "debug.h"
#include "trace.h"
#include "debug.tmh"

#include "usbip_proto.h"
#include "usbip_common.h"
#include "usbip_vhci_api.h"
#include "pdu.h"

namespace
{

void print_cmd_submit(const usbip_header_cmd_submit &c)
{
	Trace(TRACE_LEVEL_VERBOSE, 
		"cmd_submit{flags %#x, length %d, start_frame %d, isoc[%d], interval %d}",
		c.transfer_flags, 
		c.transfer_buffer_length, 
		c.start_frame, 
		c.number_of_packets, 
		c.interval);
}

void print_ret_submit(const usbip_header_ret_submit &c)
{
	Trace(TRACE_LEVEL_VERBOSE, 
		"ret_submit(status %d, actual_length %d, start_frame %d, isoc[%d], error_count %d}", 
		c.status,
		c.actual_length,
		c.start_frame,
		c.number_of_packets,
		c.error_count);
}

} // namespace


void trace(const usbip_header &hdr, const char *func, bool remote)
{
	auto &base = hdr.base;

	Trace(TRACE_LEVEL_VERBOSE, "%s(remote %d) {cmd %u, seqnum %#x, devid %#x, %s[%u]}",
					func, remote,
					base.command, 
					base.seqnum, 
					base.devid,			
					base.direction == USBIP_DIR_OUT ? "out" : "in",
					base.ep);

	switch (base.command) {
	case USBIP_CMD_SUBMIT:
		print_cmd_submit(hdr.u.cmd_submit);
		break;
	case USBIP_RET_SUBMIT:
		print_ret_submit(hdr.u.ret_submit);
		break;
	case USBIP_CMD_UNLINK:
		Trace(TRACE_LEVEL_VERBOSE, "cmd_unlink: seqnum %#x", hdr.u.cmd_unlink.seqnum);
		break;
	case USBIP_RET_UNLINK:
		Trace(TRACE_LEVEL_VERBOSE, "ret_unlink: status %d", hdr.u.ret_unlink.status);
		break;
	}
}
