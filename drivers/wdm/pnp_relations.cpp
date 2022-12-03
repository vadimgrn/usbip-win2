#include "pnp_relations.h"
#include <wdm.h>
#include "trace.h"
#include "pnp_relations.tmh"

#include "dev.h"
#include "irp.h"
#include "vhci.h"
#include "vhub.h"

namespace
{

constexpr auto SizeOf_DEVICE_RELATIONS(ULONG cnt)
{
	return sizeof(DEVICE_RELATIONS) + (cnt ? --cnt*sizeof(*DEVICE_RELATIONS::Objects) : 0);
}

static_assert(SizeOf_DEVICE_RELATIONS(0) == sizeof(DEVICE_RELATIONS));
static_assert(SizeOf_DEVICE_RELATIONS(1) == sizeof(DEVICE_RELATIONS));
static_assert(SizeOf_DEVICE_RELATIONS(2)  > sizeof(DEVICE_RELATIONS));

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE inline void log(_In_opt_ const DEVICE_RELATIONS *r, _In_ const char *what)
{
	PAGED_CODE();

	auto cnt = r ? r->Count : 0;
	TraceDbg("%s DEVICE_RELATIONS.Count %lu", what, cnt);

	for (ULONG i = 0; i < cnt; ++i) {
		TraceDbg("%04x[%lu]", ptr4log(r->Objects[i]), i);
	}
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto contains(_In_ const DEVICE_RELATIONS &r, _In_ const DEVICE_OBJECT *devobj)
{
	PAGED_CODE();
	NT_ASSERT(devobj);

	for (ULONG i = 0; i < r.Count; ++i) {
		if (r.Objects[i] == devobj) {
			return true;
		}
	}

	return false;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto reallocate(_Inout_ DEVICE_RELATIONS* &r, _In_ ULONG extra_cnt)
{
	PAGED_CODE();

	auto old_cnt = r ? r->Count : 0;

	auto alloc_cnt = old_cnt + extra_cnt;
	if (!alloc_cnt) {
		alloc_cnt = 1;
	}

	if (old_cnt == alloc_cnt) {
		NT_ASSERT(!extra_cnt);
		return STATUS_SUCCESS;
	}

	auto sz = SizeOf_DEVICE_RELATIONS(alloc_cnt);

	auto new_r = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED, sz, USBIP_VHCI_POOL_TAG);
	if (!new_r) {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", sz);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	new_r->Count = old_cnt;

	if (r) {
		RtlCopyMemory(new_r->Objects, r->Objects, r->Count*sizeof(*r->Objects));
		ExFreePool(r);
	}

	r = new_r;
	return STATUS_SUCCESS;
}


_IRQL_requires_(PASSIVE_LEVEL)
auto approve(_In_opt_ vdev_t *vdev, _In_opt_ DEVICE_RELATIONS *r)
{
	PAGED_CODE();
	return vdev && vdev->PnPState != pnp_state::Removed && !(r && contains(*r, vdev->Self));
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto append(_Inout_ DEVICE_RELATIONS* &r, _In_ vdev_t* vdev[], _In_ ULONG cnt)
{
	PAGED_CODE();

	if (auto err = reallocate(r, cnt)) {
		return err;
	}

	for (ULONG i = 0; i < cnt; ++i) {
		auto obj = vdev[i]->Self;
		ObReferenceObject(obj);
		r->Objects[r->Count++] = obj;
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto bus_new_relations(_In_ root_dev_t &root, _Inout_ DEVICE_RELATIONS* &r)
{
	PAGED_CODE();

	ULONG cnt = 0;
	vdev_t* objects[ARRAYSIZE(root.children_pdo)];
	
	for (auto child: root.children_pdo) {
		if (approve(child, r)) {
			objects[cnt++] = child;
		}
	}

	return append(r, objects, cnt);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto bus_new_relations(_In_ vhci_dev_t &vhci, _Inout_ DEVICE_RELATIONS* &r)
{
	PAGED_CODE();
	auto ok = approve(vhci.child_pdo, r);
	return append(r, &vhci.child_pdo, ok);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE vpdo_dev_t *find_vpdo(_In_ const vhub_dev_t &vhub, _In_ const DEVICE_OBJECT *devobj)
{
	PAGED_CODE();
	NT_ASSERT(devobj);

	for (auto i: vhub.vpdo) {
		if (i && i->Self == devobj) {
			return i;
		}
	}

	return nullptr;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto bus_new_relations(_In_ vhub_dev_t &vhub, _Inout_ DEVICE_RELATIONS* &r)
{
	PAGED_CODE();

	auto r_cnt = r ? r->Count : 0;
	int plugged = 0;

	ExAcquireFastMutex(&vhub.mutex);

	for (auto i: vhub.vpdo) { 
		if (i && !i->unplugged) {
			++plugged;
		}
	}

	auto new_cnt = r_cnt + plugged;
	auto size = SizeOf_DEVICE_RELATIONS(new_cnt);

	auto new_r = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED, size, USBIP_VHCI_POOL_TAG);

	if (new_r) {
		new_r->Count = 0;
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", size);
		ExReleaseFastMutex(&vhub.mutex);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	for (ULONG i = 0; i < r_cnt; ++i) {
		auto devobj = r->Objects[i];
		auto vpdo = find_vpdo(vhub, devobj);

		if (vpdo && vpdo->unplugged) {
			ObDereferenceObject(devobj);
		} else {
			new_r->Objects[new_r->Count++] = devobj;
		}
	}

	for (auto vpdo: vhub.vpdo) {
		if (vpdo && !(vpdo->unplugged || contains(*new_r, vpdo->Self))) {
			new_r->Objects[new_r->Count++] = vpdo->Self;
			ObReferenceObject(vpdo->Self);
		}
	}

	NT_ASSERT(new_r->Count <= new_cnt);
	ExReleaseFastMutex(&vhub.mutex);

	if (r) {
		ExFreePool(r);
	}

	r = new_r;
	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto bus_new_relations(_In_ vdev_t &vdev, _Inout_ DEVICE_RELATIONS* &r)
{
	PAGED_CODE();

	log(r, "Old");
	auto err = STATUS_NOT_SUPPORTED;

	switch (vdev.type) {
	case VDEV_VHUB:
		err = bus_new_relations(static_cast<vhub_dev_t&>(vdev), r);
		break;
	case VDEV_VHCI:
		err = bus_new_relations(static_cast<vhci_dev_t&>(vdev), r);
		break;
	case VDEV_ROOT:
		err = bus_new_relations(static_cast<root_dev_t&>(vdev), r);
		break;
	}

	log(r, "New");
	return err;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto target_relation(_In_ vdev_t *vdev, _Inout_ DEVICE_RELATIONS* &r)
{
	PAGED_CODE();

	if (vdev->type != VDEV_VPDO) {
		return STATUS_NOT_SUPPORTED;
	}

	NT_ASSERT(!r); // only vpdo can handle this request
	if (auto err = reallocate(r, 1)) {
		return err;
	}

	return append(r, &vdev, 1);
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_query_device_relations(_In_ vdev_t *vdev, _Inout_ IRP *irp)
{
	PAGED_CODE();

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto type = irpstack->Parameters.QueryDeviceRelations.Type;

	auto r = reinterpret_cast<DEVICE_RELATIONS*>(irp->IoStatus.Information);
	NTSTATUS st{};

	switch (type) {
	case BusRelations:
		st = bus_new_relations(*vdev, r);
		break;
	case TargetDeviceRelation:
		st = target_relation(vdev, r);
		break;
	case RemovalRelations:
	case EjectionRelations:
		if (is_fdo(vdev->type)) {
			return irp_pass_down(vdev->devobj_lower, irp);
		}
		break;
	default:
		st = irp->IoStatus.Status;
	}

	TraceDbg("%!vdev_type_t!, %!_DEVICE_RELATION_TYPE! -> %!STATUS!", vdev->type, type, st);

	irp->IoStatus.Information = reinterpret_cast<ULONG_PTR>(r);
	return CompleteRequest(irp, st);
}
