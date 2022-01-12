#include "pnp_relations.h"
#include "trace.h"
#include "pnp_relations.tmh"

#include "irp.h"
#include "vhci.h"
#include "vhub.h"

namespace
{

PAGEABLE void relations_deref_devobj(PDEVICE_RELATIONS relations, ULONG idx)
{
	PAGED_CODE();

	ObDereferenceObject(relations->Objects[idx]);
	if (idx < relations->Count - 1)
		RtlCopyMemory(relations->Objects + idx, relations->Objects + idx + 1, sizeof(PDEVICE_OBJECT) * (relations->Count - 1 - idx));
}

PAGEABLE BOOLEAN relations_has_devobj(PDEVICE_RELATIONS relations, PDEVICE_OBJECT devobj, BOOLEAN deref)
{
	PAGED_CODE();

	ULONG	i;

	for (i = 0; i < relations->Count; i++) {
		if (relations->Objects[i] == devobj) {
			if (deref)
				relations_deref_devobj(relations, i);
			return TRUE;
		}
	}
	return FALSE;
}

PAGEABLE NTSTATUS get_bus_relations_1_child(vdev_t * vdev, PDEVICE_RELATIONS *pdev_relations)
{
	PAGED_CODE();

	BOOLEAN	child_exist = TRUE;
	PDEVICE_RELATIONS	relations = *pdev_relations, relations_new;
	PDEVICE_OBJECT	devobj_cpdo;
	ULONG	size;

	if (!vdev->child_pdo || vdev->child_pdo->PnPState == pnp_state::Removed)
		child_exist = FALSE;

	if (!relations) {
		relations = (DEVICE_RELATIONS*)ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), USBIP_VHCI_POOL_TAG);
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

	devobj_cpdo = vdev->child_pdo->Self;
	if (relations->Count == 0) {
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
	size = sizeof(DEVICE_RELATIONS) + relations->Count * sizeof(PDEVICE_OBJECT);
	relations_new = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, size, USBIP_VHCI_POOL_TAG);
	if (relations_new == nullptr) {
		Trace(TRACE_LEVEL_ERROR, "old relations will be used: out of memory");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlCopyMemory(relations_new->Objects, relations->Objects, sizeof(PDEVICE_OBJECT) * relations->Count);
	relations_new->Count = relations->Count + 1;
	relations_new->Objects[relations->Count] = devobj_cpdo;
	ObReferenceObject(devobj_cpdo);

	ExFreePool(relations);
	*pdev_relations = relations_new;

	return STATUS_SUCCESS;
}

vpdo_dev_t *find_managed_vpdo(vhub_dev_t *vhub, DEVICE_OBJECT *devobj)
{
	for (auto i: vhub->vpdo) {
		if (i && i->Self == devobj) {
			return i;
		}
	}

	return nullptr;
}

BOOLEAN
	is_in_dev_relations(PDEVICE_OBJECT devobjs[], ULONG n_counts, vpdo_dev_t * vpdo)
{
	ULONG	i;

	for (i = 0; i < n_counts; i++) {
		if (vpdo->Self == devobjs[i]) {
			return TRUE;
		}
	}
	return FALSE;
}

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

	auto relations = (DEVICE_RELATIONS*)ExAllocatePoolWithTag(PagedPool, length, USBIP_VHCI_POOL_TAG);
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

		if (is_in_dev_relations(relations->Objects, n_news, vpdo)) {
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

PAGEABLE PDEVICE_RELATIONS get_self_dev_relation(vdev_t * vdev)
{
	PAGED_CODE();

	PDEVICE_RELATIONS	dev_relations;

	dev_relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), USBIP_VHCI_POOL_TAG);
	if (dev_relations == nullptr)
		return nullptr;

	// There is only one vpdo in the structure
	// for this relation type. The PnP Manager removes
	// the reference to the vpdo when the driver or application
	// un-registers for notification on the device.
	dev_relations->Count = 1;
	dev_relations->Objects[0] = vdev->Self;
	ObReferenceObject(vdev->Self);

	return dev_relations;
}

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


PAGEABLE NTSTATUS pnp_query_device_relations(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);
	Trace(TRACE_LEVEL_INFORMATION, "%!vdev_type_t!: %!DEVICE_RELATION_TYPE!", vdev->type, irpstack->Parameters.QueryDeviceRelations.Type);

	DEVICE_RELATIONS *dev_relations = (DEVICE_RELATIONS*)irp->IoStatus.Information;
	NTSTATUS status = STATUS_SUCCESS;

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
		status = STATUS_SUCCESS;
		break;
	default:
		Trace(TRACE_LEVEL_INFORMATION, "skip: %!DEVICE_RELATION_TYPE!", irpstack->Parameters.QueryDeviceRelations.Type);
		status = irp->IoStatus.Status;
	}

	if (NT_SUCCESS(status)) {
		irp->IoStatus.Information = (ULONG_PTR)dev_relations;
	}

	return irp_done(irp, status);
}
