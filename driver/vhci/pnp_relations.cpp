#include "pnp_relations.h"
#include <wdm.h>
#include "trace.h"
#include "pnp_relations.tmh"

#include "irp.h"
#include "vhci.h"
#include "vhub.h"

namespace
{

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void relations_deref_devobj(PDEVICE_RELATIONS relations, ULONG idx)
{
	PAGED_CODE();

	ObDereferenceObject(relations->Objects[idx]);
	if (idx < relations->Count - 1) {
		RtlCopyMemory(relations->Objects + idx, relations->Objects + idx + 1, sizeof(PDEVICE_OBJECT) * (relations->Count - 1 - idx));
	}
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto relations_has_devobj(DEVICE_RELATIONS *relations, DEVICE_OBJECT *devobj, bool deref)
{
	PAGED_CODE();

	for (ULONG i = 0; i < relations->Count; ++i) {
		if (relations->Objects[i] == devobj) {
			if (deref) {
				relations_deref_devobj(relations, i);
			}
			return true;
		}
	}
	return false;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_bus_relations_1_child(vdev_t *vdev, PDEVICE_RELATIONS *pdev_relations)
{
	PAGED_CODE();

	bool child_exist = true;
	auto relations = *pdev_relations;

	if (!vdev->child_pdo || vdev->child_pdo->PnPState == pnp_state::Removed) {
		child_exist = false;
        }

	if (!relations) {
		relations = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED|POOL_FLAG_UNINITIALIZED, sizeof(*relations), USBIP_VHCI_POOL_TAG);
		if (!relations) {
			Trace(TRACE_LEVEL_ERROR, "no relations will be reported: out of memory");
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		relations->Count = 0;
	}
	if (!child_exist) {
		*pdev_relations = relations;
		return STATUS_SUCCESS;
	}

	auto devobj_cpdo = vdev->child_pdo->Self;
	if (!relations->Count) {
		*pdev_relations = relations;
		relations->Count = 1;
		relations->Objects[0] = devobj_cpdo;
		ObReferenceObject(devobj_cpdo);
		return STATUS_SUCCESS;
	}
	if (relations_has_devobj(relations, devobj_cpdo, !child_exist)) {
		*pdev_relations = relations;
		return STATUS_SUCCESS;
	}

	// Need to allocate a new relations structure and add vhub to it
	ULONG size = sizeof(*relations) + relations->Count*sizeof(*relations->Objects);
	auto relations_new = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED, size, USBIP_VHCI_POOL_TAG);
	if (!relations_new) {
		Trace(TRACE_LEVEL_ERROR, "old relations will be used: out of memory");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlCopyMemory(relations_new->Objects, relations->Objects, sizeof(*relations->Objects)*relations->Count);
	relations_new->Count = relations->Count + 1;
	relations_new->Objects[relations->Count] = devobj_cpdo;
	ObReferenceObject(devobj_cpdo);

	ExFreePoolWithTag(relations, USBIP_VHCI_POOL_TAG);
	*pdev_relations = relations_new;

	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE vpdo_dev_t *find_managed_vpdo(vhub_dev_t *vhub, DEVICE_OBJECT *devobj)
{
	PAGED_CODE();

	for (auto i: vhub->vpdo) {
		if (i && i->Self == devobj) {
			return i;
		}
	}

	return nullptr;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto is_in_dev_relations(DEVICE_OBJECT **dev, ULONG cnt, const vpdo_dev_t &vpdo)
{
	PAGED_CODE();

	for (ULONG i = 0; i < cnt; ++i) {
		if (vpdo.Self == dev[i]) {
			return true;
		}
	}

	return false;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_bus_relations_vhub(vhub_dev_t *vhub, PDEVICE_RELATIONS *pdev_relations)
{
	PAGED_CODE();

	auto relations_old = *pdev_relations;

	ULONG n_olds = 0;
	ULONG n_news = 0;

	ExAcquireFastMutex(&vhub->Mutex);

	if (relations_old) {
		n_olds = relations_old->Count;
	}
	
	int total = 0;
	int plugged = 0;
	for (auto i: vhub->vpdo) { 
		if (i) {
			plugged += !i->unplugged;
			++total;
		}
	}

	// Need to allocate a new relations structure and add our vpdos to it
	ULONG length = sizeof(DEVICE_RELATIONS) + (plugged + n_olds - 1) * sizeof(PDEVICE_OBJECT);

	auto relations = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED|POOL_FLAG_UNINITIALIZED, length, USBIP_VHCI_POOL_TAG);
	if (!relations) {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %lu bytes", length);
		ExReleaseFastMutex(&vhub->Mutex);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	for (ULONG i = 0; i < n_olds; ++i) {
		auto devobj = relations_old->Objects[i];
		auto vpdo = find_managed_vpdo(vhub, devobj);
		if (vpdo && vpdo->unplugged) {
			ObDereferenceObject(devobj);
		} else {
			relations->Objects[n_news] = devobj;
			++n_news;
		}
	}

	for (auto vpdo: vhub->vpdo) {

		if (!vpdo) {
			continue;
		}

		if (is_in_dev_relations(relations->Objects, n_news, *vpdo)) {
			continue;
		}

		if (!vpdo->unplugged) {
			relations->Objects[n_news++] = vpdo->Self;
			ObReferenceObject(vpdo->Self);
		}
	}

	relations->Count = n_news;

	Trace(TRACE_LEVEL_INFORMATION, "vhub vpdos: total %d, plugged %d; bus relations: old %lu, new %lu", 
					total, plugged, n_olds, n_news);

	if (relations_old) {
		ExFreePool(relations_old);
	}

	ExReleaseFastMutex(&vhub->Mutex);

	*pdev_relations = relations;
	return STATUS_SUCCESS;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE DEVICE_RELATIONS *get_self_dev_relation(vdev_t *vdev)
{
	PAGED_CODE();

	auto dev_relations = (DEVICE_RELATIONS*)ExAllocatePool2(POOL_FLAG_PAGED|POOL_FLAG_UNINITIALIZED, sizeof(DEVICE_RELATIONS), USBIP_VHCI_POOL_TAG);
	if (!dev_relations) {
		return nullptr;
        }

	// There is only one vpdo in the structure
	// for this relation type. The PnP Manager removes
	// the reference to the vpdo when the driver or application
	// un-registers for notification on the device.
	dev_relations->Count = 1;
	dev_relations->Objects[0] = vdev->Self;
	ObReferenceObject(vdev->Self);

	return dev_relations;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_bus_relations(vdev_t * vdev, PDEVICE_RELATIONS *pdev_relations)
{
	PAGED_CODE();

	switch (vdev->type) {
	case VDEV_ROOT:
	case VDEV_VHCI:
		return get_bus_relations_1_child(vdev, pdev_relations);
	case VDEV_VHUB:
		return get_bus_relations_vhub((vhub_dev_t *)vdev, pdev_relations);
	default:
		return STATUS_NOT_SUPPORTED;
	}
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS get_target_relation(vdev_t * vdev, PDEVICE_RELATIONS *pdev_relations)
{
	PAGED_CODE();

	if (vdev->type != VDEV_VPDO)
		return STATUS_NOT_SUPPORTED;

	if (*pdev_relations) {
		// Only vpdo can handle this request. Somebody above
		// is not playing by rule.
		NT_ASSERT(!"Someone above is handling TagerDeviceRelation");
	}
	*pdev_relations = get_self_dev_relation(vdev);
	return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS pnp_query_device_relations(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: %!DEVICE_RELATION_TYPE!", vdev->type, irpstack->Parameters.QueryDeviceRelations.Type);

	auto dev_relations = (DEVICE_RELATIONS*)irp->IoStatus.Information;
	NTSTATUS status{};

	switch (irpstack->Parameters.QueryDeviceRelations.Type) {
	case TargetDeviceRelation:
		status = get_target_relation(vdev, &dev_relations);
		break;
	case BusRelations:
		status = get_bus_relations(vdev, &dev_relations);
		break;
	case RemovalRelations:
	case EjectionRelations:
		if (is_fdo(vdev->type)) {
			return irp_pass_down(vdev->devobj_lower, irp);
		}
		break;
	default:
		Trace(TRACE_LEVEL_INFORMATION, "skip: %!DEVICE_RELATION_TYPE!", irpstack->Parameters.QueryDeviceRelations.Type);
		status = irp->IoStatus.Status;
	}

	if (NT_SUCCESS(status)) {
		irp->IoStatus.Information = (ULONG_PTR)dev_relations;
	}

	return CompleteRequest(irp, status);
}
