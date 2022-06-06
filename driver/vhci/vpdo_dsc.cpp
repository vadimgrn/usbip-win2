#include "vpdo_dsc.h"
#include "trace.h"
#include "vpdo_dsc.tmh"

#include "vhci.h"
#include "dev.h"
#include "usbip_proto.h"
#include "irp.h"
#include "usbdsc.h"
#include "internal_ioctl.h"

namespace
{

PAGEABLE USB_COMMON_DESCRIPTOR *find_descriptor(USB_CONFIGURATION_DESCRIPTOR *cd, UCHAR type, UCHAR index)
{
	PAGED_CODE();

	USB_COMMON_DESCRIPTOR *from{};
	auto end = reinterpret_cast<char*>(cd + cd->wTotalLength);

	for (int i = 0; (char*)from < end; ++i) {
		from = dsc_find_next(cd, from, type);
		if (!from) {
			break;
		}
		if (i == index) {
			NT_ASSERT(from->bDescriptorType == type);
			return from;
		}
	}

	return nullptr;
}

} // namespace


PAGEABLE NTSTATUS get_descr_from_nodeconn(vpdo_dev_t *vpdo, USB_DESCRIPTOR_REQUEST &r, ULONG &outlen)
{
	PAGED_CODE();

	auto setup = (USB_DEFAULT_PIPE_SETUP_PACKET*)&r.SetupPacket;
	static_assert(sizeof(*setup) == sizeof(r.SetupPacket));

	auto cfg = vpdo->actconfig ? vpdo->actconfig->bConfigurationValue : 0;
	auto index = setup->wValue.LowByte;
//	auto lang_id = setup->wIndex.W;

	void *dsc_data{};
	USHORT dsc_len = 0;

	switch (auto type = setup->wValue.HiByte) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		dsc_data = &vpdo->descriptor;
		dsc_len = vpdo->descriptor.bLength;
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		if (cfg > 0 && cfg - 1 == index) { // FIXME: can be wrong assumption
			dsc_data = vpdo->actconfig;
			dsc_len = vpdo->actconfig->wTotalLength;
		} else {
			TraceDbg("bConfigurationValue(%d) - 1 != Index(%d)", cfg, index);
		}
		break;
	case USB_STRING_DESCRIPTOR_TYPE: // lang_id is ignored
		if (index < ARRAYSIZE(vpdo->strings)) {
			if (auto d = vpdo->strings[index]) {
				dsc_len = d->bLength;
				dsc_data = d;
				break;
			}
		}
		[[fallthrough]];
	case USB_INTERFACE_DESCRIPTOR_TYPE:
	case USB_ENDPOINT_DESCRIPTOR_TYPE:
		if (auto cd = vpdo->actconfig) {
			if (auto d = find_descriptor(cd, type, index)) {
				dsc_len = d->bLength;
				dsc_data = d;
			}
		}
		break;
	}

	if (!dsc_data) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	NT_ASSERT(outlen > sizeof(r));
	auto data_sz = outlen - ULONG(sizeof(r)); // r.Data[]

	auto cnt = min(data_sz, dsc_len);
	RtlCopyMemory(r.Data, dsc_data, cnt);
	outlen = sizeof(r) + cnt;

	TraceDbg("%lu bytes%!BIN!", cnt, WppBinary(dsc_data, (USHORT)cnt));
	return STATUS_SUCCESS;
}
