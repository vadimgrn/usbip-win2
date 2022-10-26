#pragma once

#include <wdm.h>
#include <usbip\proto.h>

struct _USB_ENDPOINT_DESCRIPTOR;

namespace usbip
{

struct device_ctx;

class setup_dir
{
public:
	constexpr setup_dir() = default;
	constexpr setup_dir(bool dir_out) : val((1 << int(dir_out)) | 1) {}

	constexpr explicit operator bool() const { return val & 1; }
	constexpr auto operator !() const { return !static_cast<bool>(*this); }

	constexpr auto operator ==(setup_dir d) const { return val == d.val; }
	constexpr auto operator !=(setup_dir d) const { return val != d.val; }

	constexpr bool operator *() const { return val >> 1; }

	static constexpr auto in() { return setup_dir(false); }
	static constexpr auto out() { return setup_dir(true); }

private:
	int val{};
};

static_assert(!bool(setup_dir()));
static_assert(!setup_dir());

static_assert(bool(setup_dir::in()));
static_assert(!!setup_dir::in());
static_assert(!*setup_dir::in());

static_assert(bool(setup_dir::out()));
static_assert(!!setup_dir::out());
static_assert(*setup_dir::out());


_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS set_cmd_submit_usbip_header(
	_Out_ usbip_header &hdr, _Inout_ device_ctx &dev, _In_ const _USB_ENDPOINT_DESCRIPTOR &epd,
	_In_ ULONG TransferFlags, _In_ ULONG TransferBufferLength = 0, _In_ setup_dir setup_dir_out = setup_dir());

_IRQL_requires_max_(DISPATCH_LEVEL)
void set_cmd_unlink_usbip_header(_Out_ usbip_header &hdr, _Inout_ device_ctx &dev, _In_ seqnum_t seqnum_unlink);

} // namespace usbip
