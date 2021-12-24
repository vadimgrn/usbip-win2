#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <usbdi.h>

#include "usb_util.h"

#include "vhci_dev.h"

struct urb_req
{
	vpdo_dev_t *vpdo;
	IRP *irp;
	KEVENT *event;
	unsigned long seq_num;
	unsigned long seq_num_unlink;
	LIST_ENTRY list_all;
	LIST_ENTRY list_state;
};

#define RemoveEntryListInit(le) \
do { RemoveEntryList(le); InitializeListHead(le); } while (0)

NTSTATUS submit_urbr(vpdo_dev_t *vpdo, struct urb_req *urbr);

struct urb_req *create_urbr(vpdo_dev_t *vpdo, IRP *irp, unsigned long seq_num_unlink);
void free_urbr(struct urb_req *urbr);

bool is_port_urbr(IRP *irp, USBD_PIPE_HANDLE handle);

struct urb_req *find_sent_urbr(pvpdo_dev_t vpdo, unsigned long seqnum);
struct urb_req *find_pending_urbr(pvpdo_dev_t vpdo);

enum { URB_REQ_STR_BUFSZ = 64 };
const char* urb_req_str(char* buf, size_t len, const struct urb_req *urbr);

#ifdef __cplusplus
}
#endif
