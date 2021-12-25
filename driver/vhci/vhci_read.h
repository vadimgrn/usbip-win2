#pragma once

#include "usbreq.h"

NTSTATUS store_urbr(IRP *irp, struct urb_req *urbr);
