#pragma once

#include <ntddk.h>
#include <wdf.h>

EXTERN_C_START

#define MAX_HUB_20PORTS		12
#define MAX_HUB_30PORTS		4

struct _ctx_vusb;

typedef struct
{
	WDFDEVICE	hdev;
	ULONG		n_max_ports, n_used_ports;
	WDFQUEUE	queue;
	struct _ctx_vusb	**vusbs;
	WDFSPINLOCK	spin_lock;
} ctx_vhci_t, *pctx_vhci_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ctx_vhci_t, TO_VHCI)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(pctx_vhci_t, TO_PVHCI)

struct _urb_req;
struct _ctx_ep;

typedef struct _ctx_vusb
{
	ULONG		port;
	pctx_vhci_t	vhci;
	UDECXUSBDEVICE	ude_usbdev;
	/*
	 * There's 2 endpoint allocation types for UDE device: Simple & Dynamic
	 * vusb will try to create an EP in a simple way if possible.
	 * This approach is based on the belief that a simple type is literally simple.
	 */
	BOOLEAN		is_simple_ep_alloc;
	BOOLEAN		invalid;
	/* reference count to prevent from removal while being used */
	ULONG		refcnt;

	// pending req which doesn't find an available urbr
	WDFREQUEST	pending_req_read;
	// a partially transferred urbr
	struct _urb_req	*urbr_sent_partial;
	// a partially transferred length of urbr_sent_partial
	ULONG		len_sent_partial;
	// all urbr's. This list will be used for clear or cancellation.
	LIST_ENTRY	head_urbr;
	// pending urbr's which are not transferred yet
	LIST_ENTRY	head_urbr_pending;
	// urbr's which had been sent and have waited for response
	LIST_ENTRY	head_urbr_sent;
	ULONG		devid;
	ULONG		seq_num;
	struct _ctx_ep	*ep_default;
	WDFLOOKASIDE	lookaside_urbr;
	WDFSPINLOCK	spin_lock;

	/* keep these usbip port command */
	USHORT		id_vendor, id_product, dev_speed;

	// string index for USB serial
	UCHAR		iSerial;
	// USB serial string
	PWCHAR		wserial;

	/* Full-length configuration descriptor for finding an interface descriptor of EP */
	PUCHAR		dsc_conf;
	/* alt settings for interface. use SHORT for treating unassigned case(-1) */
	PSHORT		intf_altsettings;
	/* a first value in configuration descriptor */
	UCHAR		default_conf_value;
} ctx_vusb_t, *pctx_vusb_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ctx_vusb_t, TO_VUSB)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(pctx_vusb_t, TO_PVUSB)

typedef struct _ctx_ep
{
	pctx_vusb_t	vusb;
	UCHAR		type, addr, interval;
	/*
	 * Information for a parent interface.
	 * These are used for adjusting value of interface alternate setting.
	 */
	UCHAR		intf_num, altsetting;
	UDECXUSBENDPOINT	ude_ep;
	WDFQUEUE	queue;
} ctx_ep_t, *pctx_ep_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ctx_ep_t, TO_EP)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(pctx_ep_t, TO_PEP)

typedef struct _ctx_safe_vusb
{
	pctx_vhci_t	vhci;
	ULONG		port;
} ctx_safe_vusb_t, *pctx_safe_vusb_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ctx_safe_vusb_t, TO_SAFE_VUSB)

#define TO_SAFE_VUSB_FROM_REQ(req)	TO_SAFE_VUSB(WdfRequestGetFileObject(req))

#define VUSB_CREATING	((pctx_vusb_t)-1)
#define VUSB_IS_VALID(vusb)	((vusb) != NULL && (vusb) != VUSB_CREATING && !(vusb)->invalid)

extern NTSTATUS plugout_vusb(pctx_vhci_t vhci, CHAR port);

extern pctx_vusb_t get_vusb(pctx_vhci_t vhci, ULONG port);
extern pctx_vusb_t get_vusb_by_req(WDFREQUEST req);
extern void put_vusb(pctx_vusb_t vusb);
extern void put_vusb_passively(pctx_vusb_t vusb);

EXTERN_C_END
