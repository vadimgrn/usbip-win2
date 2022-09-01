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
PAGEABLE auto get_bus_new_relations_1_child(_In_ vdev_t &vdev, _Inout_ DEVICE_RELATIONS* &r)
{
	PAGED_CODE();

	if (!r) {
		r = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, sizeof(*r), USBIP_VHCI_POOL_TAG);
		if (r) {
			r->Count = 0;
		} else {
			Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", sizeof(*r));
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}

	if (!vdev.child_pdo || vdev.child_pdo->PnPState == pnp_state::Removed) {
		return STATUS_SUCCESS;
	}

	auto devobj_cpdo = vdev.child_pdo->Self;

	if (!r->Count) {
		r->Count = 1;
		*r->Objects = devobj_cpdo;
		ObReferenceObject(devobj_cpdo);
		return STATUS_SUCCESS;
 	} else if (contains(*r, devobj_cpdo)) {
		return STATUS_SUCCESS;
	}

	auto new_cnt = r->Count + 1;
	auto size = SizeOf_DEVICE_RELATIONS(new_cnt);

	auto new_r = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, size, USBIP_VHCI_POOL_TAG);
	if (!new_r) {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %Iu bytes", size);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	new_r->Count = new_cnt;
	RtlCopyMemory(new_r->Objects, r->Objects, r->Count*sizeof(*r->Objects));
	new_r->Objects[r->Count] = devobj_cpdo; // last element
	ObReferenceObject(devobj_cpdo);

	ExFreePool(r);
	r = new_r;

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto get_bus_new_relations(_In_ vhub_dev_t &vhub, _Inout_ DEVICE_RELATIONS* &r)
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

	auto new_r = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, size, USBIP_VHCI_POOL_TAG);

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

	ExReleaseFastMutex(&vhub.mutex);

	if (r) {
		ExFreePool(r);
	}

	r = new_r;

	TraceMsg("Count: old %lu, new %lu, allocated %lu", r_cnt, new_r->Count, new_cnt);
	NT_ASSERT(new_r->Count <= new_cnt);

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto get_bus_new_relations(_In_ vdev_t &vdev, _Inout_ DEVICE_RELATIONS* &r)
{
	PAGED_CODE();

	switch (vdev.type) {
	case VDEV_ROOT:
	case VDEV_VHCI:
		return get_bus_new_relations_1_child(vdev, r);
	case VDEV_VHUB:
		return get_bus_new_relations(static_cast<vhub_dev_t&>(vdev), r);
	default:
		return STATUS_NOT_SUPPORTED;
	}
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto get_target_relation(_In_ vdev_t &vdev, _Inout_ DEVICE_RELATIONS* &r)
{
	PAGED_CODE();

	if (vdev.type != VDEV_VPDO) {
		return STATUS_NOT_SUPPORTED;
	}

	NT_ASSERT(!r); // only vpdo can handle this request
	r = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, sizeof(*r), USBIP_VHCI_POOL_TAG);

	if (r) {
		r->Count = 1;
		*r->Objects = vdev.Self;
		ObReferenceObject(vdev.Self);
	}

	return r ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
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
		st = get_bus_new_relations(*vdev, r);
		break;
	case TargetDeviceRelation:
		st = get_target_relation(*vdev, r);
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
