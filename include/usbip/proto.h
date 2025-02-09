/*
 * Copyright (C) 2023 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <basetsd.h>

/*
 * Declarations from <drivers/usb/usbip/usbip_common.h>
 */

enum usbip_request_type 
{
	USBIP_CMD_SUBMIT = 1,
	USBIP_CMD_UNLINK,
	USBIP_RET_SUBMIT,
	USBIP_RET_UNLINK
};

enum usbip_dir { USBIP_DIR_OUT, USBIP_DIR_IN }; // transfer direction like USB_DIR_IN, USB_DIR_OUT
enum { USBIP_MAX_ISO_PACKETS = 1024 };
enum { number_of_packets_non_isoch = -1 }; // see protocol for USBIP_CMD_SUBMIT/USBIP_RET_SUBMIT

constexpr auto is_valid_number_of_packets(int number_of_packets)
{
	return number_of_packets >= 0 && number_of_packets <= USBIP_MAX_ISO_PACKETS;
}

using seqnum_t = UINT32;


#include <PSHPACK1.H>

struct usbip_header_basic 
{
	UINT32 command; // enum usbip_request_type
	seqnum_t seqnum;
	UINT32 devid;
	UINT32 direction; // enum usbip_dir
	UINT32 ep; // endpoint number
};

/*
 * CMD_SUBMIT
 */
struct usbip_header_cmd_submit 
{
	UINT32 transfer_flags;
	INT32 transfer_buffer_length;
	INT32 start_frame;
	INT32 number_of_packets;
	INT32 interval;
	UINT8 setup[8];
};

/*
 * RET_SUBMIT
 */
struct usbip_header_ret_submit 
{
	INT32 status;
	INT32 actual_length;
	INT32 start_frame;
	INT32 number_of_packets;
	INT32 error_count;
};

/*
 * CMD_UNLINK
 */
struct usbip_header_cmd_unlink 
{
	seqnum_t seqnum;
};

/*
 * RET_UNLINK
 */
struct usbip_header_ret_unlink 
{
	INT32 status;
};

struct usbip_header 
{
	usbip_header_basic base;

	union {
		usbip_header_cmd_submit	cmd_submit;
		usbip_header_ret_submit	ret_submit;
		usbip_header_cmd_unlink	cmd_unlink;
		usbip_header_ret_unlink	ret_unlink;
	} u;
};

static_assert(sizeof(usbip_header) == 48);

struct usbip_iso_packet_descriptor 
{
	UINT32 offset;
	UINT32 length;
	UINT32 actual_length;
	UINT32 status;
};

#include <POPPACK.H>
