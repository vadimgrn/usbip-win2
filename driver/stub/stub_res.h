#pragma once

#include "stub_dev.h"
#include "usbip_proto.h"

typedef struct stub_res {
	PIRP	irp;
	struct usbip_header	header;
	PVOID	data;
	int	data_len;
	LIST_ENTRY	list;
} stub_res_t;

enum { DBG_STUB_RES_BUFSZ = 255 };
const char *dbg_stub_res(char *buf, unsigned int len, stub_res_t *sres, usbip_stub_dev_t* devstub);

stub_res_t *
create_stub_res(unsigned int cmd, unsigned long seqnum, int err, PVOID data, int data_len, ULONG n_pkts, BOOLEAN need_copy);
void free_stub_res(stub_res_t *sres);

void add_pending_stub_res(usbip_stub_dev_t *devstub, stub_res_t *sres, PIRP irp);
void del_pending_stub_res(usbip_stub_dev_t *devstub, stub_res_t *sres);
BOOLEAN cancel_pending_stub_res(usbip_stub_dev_t *devstub, unsigned int seqnum);

NTSTATUS collect_done_stub_res(usbip_stub_dev_t *devstub, PIRP irp_read);

void reply_stub_req(usbip_stub_dev_t *devstub, stub_res_t *sres);

void reply_stub_req_hdr(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum);
void reply_stub_req_err(usbip_stub_dev_t *devstub, unsigned int cmd, unsigned long seqnum, int err);
void reply_stub_req_data(usbip_stub_dev_t *devstub, unsigned long seqnum, PVOID data, int data_len, BOOLEAN need_copy);
