#include "vhci_pnp_resources.h"
#include "vhci_irp.h"
#include "vhci.h"

namespace
{

PAGEABLE PIO_RESOURCE_REQUIREMENTS_LIST get_query_empty_resource_requirements(void)
{
	PAGED_CODE();

	IO_RESOURCE_REQUIREMENTS_LIST *r = (IO_RESOURCE_REQUIREMENTS_LIST*)ExAllocatePoolWithTag(PagedPool, sizeof(*r), USBIP_VHCI_POOL_TAG);
	if (r) {
		r->ListSize = sizeof(*r);
		r->BusNumber = 10;
		r->SlotNumber = 1;
		r->AlternativeLists = 0;
		r->List[0].Version = 1;
		r->List[0].Revision = 1;
		r->List[0].Count = 0;
	}

	return r;
}

PAGEABLE PCM_RESOURCE_LIST get_query_empty_resources(void)
{
	PAGED_CODE();

	CM_RESOURCE_LIST *r = (CM_RESOURCE_LIST*)ExAllocatePoolWithTag(PagedPool, sizeof(*r), USBIP_VHCI_POOL_TAG);
	if (r) {
		r->Count = 0;
	}

	return r;
}


} // namespace


PAGEABLE NTSTATUS pnp_query_resource_requirements(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	if (!is_fdo(vdev->type)) {
		irp->IoStatus.Information = (ULONG_PTR)get_query_empty_resource_requirements();
		return irp_done_success(irp);
	} else {
		return irp_pass_down(vdev->devobj_lower, irp);
	}
}

PAGEABLE NTSTATUS pnp_query_resources(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	if (!is_fdo(vdev->type)) {
		irp->IoStatus.Information = (ULONG_PTR)get_query_empty_resources();
		return irp_done_success(irp);
	} else {
		return irp_pass_down(vdev->devobj_lower, irp);
	}
}

PAGEABLE NTSTATUS pnp_filter_resource_requirements(vdev_t * vdev, PIRP irp)
{
	PAGED_CODE();

	if (is_fdo(vdev->type)) {
		irp->IoStatus.Information = (ULONG_PTR)get_query_empty_resource_requirements();
		return irp_done_success(irp);
	} else {
		return irp_done(irp, STATUS_NOT_SUPPORTED);
	}
}
