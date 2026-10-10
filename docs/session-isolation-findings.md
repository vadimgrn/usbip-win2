# Session-Isolation Findings (Phase 1)

Read-only exploration of the `usbip-win2` codebase to determine what it would take to make
each Windows Terminal-Server (RDS) session's USB/IP attachments invisible and untouchable to
every other session. This is the deliverable for **Plan.md Phase 1**; it changes no code.

**Headline result:** the driver has **no session, token, SID, or requestor-mode awareness of any
kind**. A single machine-wide device object, guarded by one `Everyone: GENERIC_READ|GENERIC_WRITE`
SDDL, exposes every attach/detach/list/persist operation to every user in every session, with
zero ownership tracking. There is no isolation to weaken — it must be built from nothing.

All file:line references are against the tree at the time of writing (branch `master`,
`usbip2_ude.sys` = `drivers/ude/`, `usbip2_filter.sys` = `drivers/ude_filter/`).

---

## 1. IOCTL surface and caller-identity checks

The user-facing driver `usbip2_ude.sys` exposes exactly one device object (a WDF device that is
also a UdeCx USB host controller) with a single device-interface GUID. Everything goes through
eight IOCTLs plus a `ReadFile`-based event stream.

**IOCTL codes** — `include/usbip/vhci.h:99-127`. Every code is built with:

```cpp
CTL_CODE(FILE_DEVICE_UNKNOWN, id, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
```

so the required access for *all* of them — including the destructive ones — is just
`FILE_READ_DATA | FILE_WRITE_DATA`. There is **no admin-vs-user or read-vs-write access split** at
the IOCTL level.

| IOCTL (code) | Purpose | Kernel handler (file:line) | Identity / session check |
|---|---|---|---|
| `PLUGIN_HARDWARE` 0x800 | Attach a remote USB device (opens TCP to a caller-supplied host, plugs it in) | dispatch `vhci_ioctl.cpp:839`; handler `vhci_ioctl.cpp:623` → `:543` | **None** |
| `PLUGIN_HARDWARE_ONCE` 0x806 | Attach, single attempt (no retry) | dispatch `vhci_ioctl.cpp:836`; handler `vhci_ioctl.cpp:623` | **None** |
| `PLUGOUT_HARDWARE` 0x801 | Detach one port — **or ALL devices if `port <= 0`** | dispatch `vhci_ioctl.cpp:845`; handler `vhci_ioctl.cpp:653` | **None** (keyed on an integer port only) |
| `PLUGOUT_HARDWARE_AND_REATTACH` 0x807 | Detach + reattach ("internal only" per comment, but reachable via the same switch) | dispatch `vhci_ioctl.cpp:842`; handler `vhci_ioctl.cpp:653` | **None** |
| `GET_IMPORTED_DEVICES` 0x802 | List every imported device (host/busid/serial/VID/PID) | dispatch `vhci_ioctl.cpp:885`; handler `vhci_ioctl.cpp:687` | **None** (enumerates all ports) |
| `SET_PERSISTENT` 0x803 | Overwrite the machine-wide auto-reattach registry list | dispatch `vhci_ioctl.cpp:887`; handler `vhci_ioctl.cpp:738` | **None** |
| `GET_PERSISTENT` 0x804 | Read the persistent auto-reattach list | dispatch `vhci_ioctl.cpp:889`; handler `vhci_ioctl.cpp:768` | **None** |
| `STOP_ATTACH_ATTEMPTS` 0x805 | Cancel pending/retrying attach attempts (all or matching) | dispatch `vhci_ioctl.cpp:891`; handler `vhci_ioctl.cpp:579` | **None** |
| `IOCTL_USB_USER_REQUEST` (MS std) | USB user request | `vhci_ioctl.cpp:848` → `UdecxWdfDeviceTryHandleUserIoctl` `:860` | **None** (delegated to `udecx.sys`) |
| `IRP_MJ_READ` (event stream) | Subscribe to device-state change events | `device_read` `vhci_ioctl.cpp:940`; broadcast `vhci.cpp:1012` → `process_event` `vhci.cpp:821` | **None** (fan-out to ALL subscribers in ALL sessions) |

Dispatch is registered in `create_queues()` (`vhci_ioctl.cpp:981-1015`): a default sequential queue
(`device_control`, `vhci_ioctl.cpp:994`) and a parallel queue (`device_control_parallel`,
`vhci_ioctl.cpp:1004`). Neither queue sets any per-request security.

Two handlers are worth calling out as concrete cross-session attacks that the isolation work must
close:

- **Mass detach.** `plugout_hardware` (`vhci_ioctl.cpp:671-673`): when `r->port <= 0` it calls
  `vhci::detach_all_devices(...)` — any user in any session can rip out **every** device attached by
  **every** session with one IOCTL. Otherwise it acts on a bare integer port with no owner check.
- **Enumerate everything.** `get_imported_devices` (`vhci_ioctl.cpp:714-720`) loops
  `for (int port = 1; port <= ctx.devices_cnt; ++port)` and returns each device's remote host, busid,
  serial, VID/PID — a full cross-session information disclosure.

**Userspace callers** (all open the handle with `GENERIC_READ | GENERIC_WRITE`, no elevation, no
session context — `userspace/libusbip/src/vhci.cpp:273-279`): the `libusbip` SDK wraps every IOCTL
(`vhci.cpp`, `persistent.cpp`), and `usbip.exe` (`userspace/usbip/`) and `wusbip` GUI
(`userspace/wusbip/`) call through it. None of them pass identity, and the driver would ignore it if
they did.

---

## 2. Existing session-awareness

**Zero.** `rg -i "SessionId|IoGetRequestorSessionId|PsGetProcessSessionId|WTSGetActiveConsoleSessionId|PsGetCurrentProcessSessionId"` across `drivers/ userspace/ include/` returns **no hits**. A broader
sweep for `RequestorMode|RequestorSid|QueryInformationToken|SeAccessCheck|Impersonat*|TokenUser` finds
only unrelated kernel plumbing (WSK `KernelMode` wait arguments, a `nullptr` socket SecurityDescriptor
at `drivers/libdrv/wsk_cpp.cpp:485`). No IOCTL handler ever inspects the caller.

The file-open handler `device_file_create` (`drivers/ude/vhci.cpp:514-535`) does no caller check
either — it links the new file object into a machine-wide list and completes with success.

Confirmed locally: `grep -rn "IoGetRequestorSessionId\|PsGetCurrentProcessSessionId\|WdfRequestGetRequestorMode\|SeAccessCheck" drivers/` → nothing outside `wsk_cpp`.

---

## 3. Device-interface security descriptor — can user B open the handle today?

**Yes, trivially.** The only security assignment in the entire driver is one SDDL string —
`drivers/ude/vhci.cpp:602-604`:

```cpp
WdfDeviceInitSetCharacteristics(init, FILE_AUTOGENERATED_DEVICE_NAME, true);
WdfDeviceInitAssignSDDLString(init, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
```

`SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R` is not redefined anywhere in the repo, so it resolves to
the stock WDK macro from `wdmsec.h`:

```
D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GRGW;;;WD)(A;;GR;;;RC)
```

| ACE | Principal | SID | Access |
|---|---|---|---|
| `(A;;GA;;;SY)` | Local System | S-1-5-18 | GENERIC_ALL |
| `(A;;GRGWGX;;;BA)` | Administrators | S-1-5-32-544 | GENERIC_READ/WRITE/EXECUTE |
| `(A;;GRGW;;;WD)` | **Everyone / World** | **S-1-1-0** | **GENERIC_READ + GENERIC_WRITE** |
| `(A;;GR;;;RC)` | Restricted Code | S-1-5-12 | GENERIC_READ |

`WD` (Everyone) is machine-wide and session-agnostic, and `GRGW` satisfies the
`FILE_READ_DATA|FILE_WRITE_DATA` that every IOCTL requires. So **any non-admin user in any RDS session
can open the device and drive the entire IOCTL surface** — attach, detach, mass-detach, list,
set/get persistent, cancel attempts. The two device interfaces created at `vhci.cpp:373-383`
(`WdfDeviceCreateDeviceInterface`, the standard `GUID_DEVINTERFACE_USB_HOST_CONTROLLER` plus a custom
one) set no per-interface security, and neither INF adds an `HKR,,Security` override, so they inherit
this device-object SDDL.

---

## 4. Where state lives

There are two distinct notions of "the device list":

**Runtime (in-memory).** The attached devices live in a fixed port array in the controller context,
`vhci_ctx.devices` (`drivers/ude/context.h:39-43`), a `UDECXUSBDEVICE[]` of size `devices_cnt`
protected by `devices_lock`. A device claims the first free slot in its speed range
(`claim_roothub_port`, `vhci.cpp:849-877`), keyed by 1-based `port`.

**Persisted (registry).** The auto-reattach list is a single machine-wide value:

- Key: opened via `WdfDriverOpenPersistentStateRegistryKey` (`drivers/ude/persistent.cpp:436`), which
  maps to **`HKLM\SYSTEM\CurrentControlSet\Services\usbip2_ude\State`** (service name from
  `usbip2_ude.inf:40`).
- Value: `PersistentDevices`, `REG_MULTI_SZ` (`include/usbip/consts.h:12`).
- Record format, one string per device: `host,service,busid,serial,use_wsk_events`
  (built in `userspace/libusbip/src/persistent.cpp:45`, parsed in `drivers/ude/persistent.cpp:171-184`).
- Written by the **driver** (running as SYSTEM) in response to `SET_PERSISTENT`
  (`vhci_ioctl.cpp:749-756`), reloaded at boot by `plugin_persistent_devices`
  (`persistent.cpp:119-162, 508-529`). Userspace never touches the registry directly — it only issues
  the IOCTL. Because the driver is SYSTEM, the key's ACL is irrelevant: **any user who can open the
  handle (Everyone) can cause a machine-wide persistent write.**

There is **no per-user / per-SID branching anywhere** — the list is entirely machine-wide. (Read-only
tuning values such as `NumberOfUsb20Ports` live under `...\usbip2_ude\Parameters` via
`WdfDriverOpenParametersRegistryKey`, `persistent.cpp:434`; not device state.)

**The struct to tag.** A future "owning session" field belongs on the per-device context
`device_ctx` (`drivers/ude/context.h:129-169`), right next to `int port;` (`context.h:157`). This is
the kernel object created per attached `UDECXUSBDEVICE` (`device.cpp:651-666`, parented to the one
`vhci`). The wire/IOCTL record streamed to userspace — `device_state`
(`include/usbip/vhci.h:90-94`, embedding `imported_device_location` + `imported_device_properties`) —
carries a `source_id` but **no session/owner field**, so both `GET_IMPORTED_DEVICES` and the event
stream would need a new gate. Adding an owner to the *persisted* record additionally means changing
the MULTI_SZ format in `userspace/libusbip/src/persistent.cpp:45` and `drivers/ude/persistent.cpp:171-184`.

---

## 5. UdeCx controller / device model and the install model

**One controller per devnode.** There is exactly one `WdfDeviceCreate` in the whole driver, in
`create_vhci` (`vhci.cpp:690-712`), called once from `EvtDriverDeviceAdd` (`DeviceAdd`,
`vhci.cpp:1042-1062`). It is registered as a UdeCx host controller with a fixed root-hub port count
via `UdecxWdfDeviceAddUsbDeviceEmulation` (`vhci.cpp:620-641`). Attached devices are UdeCx
`UDECXUSBDEVICE` children of that controller (`UdecxUsbDeviceCreate`, `device.cpp:651-666`;
`UdecxUsbDevicePlugIn`, `vhci_ioctl.cpp:192-203`).

**But the "single controller" is only an installer convention, not a structural constraint.** This is
the single most important finding for the Option A/B decision:

- The device is **root-enumerated** as `ROOT\USBIP_WIN2\UDE` (`drivers/ude/usbip2_ude.inf:26-30`,
  `Class=USB`). The INF does **not** contain any `SingleInstance`/exclusive directive — it merely maps
  the hardware ID to the install section.
- The userspace creator `install_devnode_and_driver` (`userspace/devnode/main.cpp:161-215`) uses
  SetupAPI with **`DICD_GENERATE_ID`**, i.e. it mints a *fresh unique instance id* on each call. There
  is no `SwDeviceCreate` anywhere.
- "Exactly one" is enforced solely by the InnoSetup installer calling `devnode.exe install ... ROOT\USBIP_WIN2\UDE`
  once (`userspace/innosetup/setup.iss:158`).

So running `devnode.exe install ROOT\USBIP_WIN2\UDE` **N times creates N independent controllers**,
each with its own `WdfDeviceCreate`, its own `vhci_ctx`, its own port array, and its own device
interface instance — with no change to the driver's core model. That makes a per-session controller
(Plan.md Option B) *structurally* far cheaper than the plan feared.

**The catch:** those instances are still created in the **machine-wide PnP tree**. Root-enumeration
does not scope a devnode to the creating session, so per-controller does not, by itself, hide anything
from another session's Device Manager. (Companion filter `usbip2_filter.sys` is a separate
`Class=Extension` upper filter bound to `USB\ROOT_HUB30` — `usbip2_filter.inf:9-31` — with no
user-facing IOCTL surface and no controller of its own; `int_dev_ctrl.cpp:347` handles only
kernel-to-kernel `IOCTL_INTERNAL_USB_SUBMIT_URB`.)

---

## 6. Globally-named kernel / IPC objects (cross-session leak surface)

**None.** A sweep for `ZwCreateEvent|ExCreateNamedEvent|IoCreateNotificationEvent|IoCreateSynchronizationEvent|CreateMutex|CreateEvent|CreateFileMapping|CreateSemaphore|\BaseNamedObjects|Global\|\Sessions`
across `drivers/`, `userspace/`, `include/`:

- **Kernel (`drivers/`): zero named objects.** In-driver synchronization is WDF spinlocks/wait-locks
  (e.g. `devices_lock`), never named events. The controller device name is auto-generated
  (`FILE_AUTOGENERATED_DEVICE_NAME`, `vhci.cpp:602`), reached via a device-interface GUID, not a
  guessable global name.
- **Userspace: three unnamed events only** — `CreateEvent(..., nullptr)` at
  `userspace/wusbip/utils.cpp:77` and `userspace/libusbip/src/remote.cpp:339`, and
  `WSACreateEvent()` at `remote.cpp:448` (always unnamed). All are process-local `OVERLAPPED`/socket
  events.

There is no `\BaseNamedObjects\` or `Global\` object anywhere, so **there is no named-object
cross-session leak surface**. The only shared, cross-session-reachable entity is the PnP controller
device itself, mediated by the SDDL in §3 — which is exactly the thing that is currently wide open.

---

## 7. Recommendation — Option A vs Option B

### What the evidence changes

The plan framed this as "Option A (shared driver, per-request checks) = simpler but leaves devices
*visible*; Option B (controller per session) = real isolation but heavy." Two findings reshape that:

1. **Option B is much cheaper than assumed** — the driver already supports N independent controllers
   with no core-model change (§5); the missing piece is lifecycle, not driver architecture.
2. **Option B does not buy hiding for free** — root-enumerated instances still land in the machine-wide
   PnP tree (§5), so another session's Device Manager can still enumerate them unless *also* gated.
   Neither option, on UdeCx root-enumeration, cleanly makes a device *invisible* to another session's
   Device Manager. True "cannot even see the node" isolation is the RDP/MS-RDPEUSB model of
   session-scoped device stacks — a major effort beyond either option as framed.

So the honest split is: **both options can enforce "cannot list / control / interfere" (the functional
security boundary and every functional item in the Phase 4 matrix). Neither cheaply delivers "cannot
see the PnP node exists."** That second property should be treated as a separate, explicitly-priced
decision at the Phase 2 gate — it is the expensive part, and it is expensive under *both* options.

### Recommendation: build Option A now

For the non-negotiable boundary (no session can list, control, detach, or interfere with another's
devices), **Option A is the right first build**, because the enforcement surface is small and
well-contained:

- **Tag ownership at attach.** In `plugin_hardware` (`vhci_ioctl.cpp:543/623`), capture the requestor
  session id (`IoGetRequestorSessionId` on the WDM IRP behind the `WDFREQUEST`) and store it on
  `device_ctx` next to `int port;` (`context.h:157`).
- **Gate every IOCTL** on that owner: `PLUGOUT_HARDWARE` (and the `port <= 0` mass-detach path —
  scope it to the caller's own devices), `STOP_ATTACH_ATTEMPTS`, and the internal `0x807`. Return
  `STATUS_ACCESS_DENIED` on mismatch, not silent success.
- **Filter list + event results per session.** `GET_IMPORTED_DEVICES` (`vhci_ioctl.cpp:714`) must skip
  ports the caller doesn't own; the event broadcast `process_event` (`vhci.cpp:821-839`) must deliver
  a device's state only to subscribers in that device's owning session (today it fans out to every
  `fileobject` in a single machine-wide list).
- **Per-SID persistent state.** Replace the single `PersistentDevices` value (§4) with per-SID storage
  and matching ACLs so one user can neither read nor overwrite another's auto-reattach list.

This closes every functional attack in Plan.md §10 (mass-detach, cross-session detach, enumeration
disclosure, persistent-list tampering, IOCTL fuzzing) at modest cost in one dispatch file plus the
`device_ctx` struct and the persistent-state module.

**Option B (controller-per-session) is worth layering on later** if residual Device-Manager visibility
proves unacceptable — its main new cost is infrastructure the project does not have today: a
session-lifecycle component (there is currently **no Windows service at all**; §Plan.md 2) to create a
controller on logon and tear it down on logoff **and** disconnect (which behave differently on RDS),
plus per-instance SDDLs scoping each controller to its session's users. Even then it needs per-request
checks or per-instance security to actually stop cross-session control, so it is an *addition to*
Option A, not a replacement for it.

### For the Phase 2 gate, decide explicitly:

1. Does "isolation" mean **blocked-but-visible** (Option A: fully enforced, node may still appear in
   another session's Device Manager) or **fully hidden** (needs session-scoped device stacks — large,
   and not delivered by Option B alone)?
2. Confirm the file/function list above (`vhci_ioctl.cpp` dispatch, `context.h:device_ctx`,
   `vhci.cpp:process_event`, `persistent.cpp`) as the Option A change surface before any code is
   written.

---

## 8. Post-Implementation Addendum: Hardening & In-Kernel Lifecycle Discoveries

Subsequent implementation and validation (Phases 3 and 4) yielded important discoveries that refine the findings above:

### 1. In-Kernel Session Lifecycle Notification (`IoRegisterContainerNotification`)
Section 7 assumed that detecting session logoff and disconnect would require a dedicated user-mode Windows service using `WTSRegisterSessionNotification`. 
Research into Windows kernel interfaces reveals that Windows provides native kernel-mode container notification support:
- **`IoRegisterContainerNotification`** with **`IoSessionStateNotification`** (`wdm.h`, supported since Windows 7).
- A driver registers an `IO_SESSION_NOTIFICATION_FUNCTION` with event masks including `IO_SESSION_STATE_LOGOFF_EVENT` and `IO_SESSION_STATE_TERMINATION_EVENT`.
- When called, `IoGetContainerInformation(IoSessionStateInformation, SessionObject, &info, sizeof(info))` provides the terminating `SessionId`.
- **Architectural Impact**: `usbip2_ude.sys` can directly detect user logoff and invoke `detach_all_devices(vhci, true, session_id)` purely inside the kernel driver. No user-mode service, daemon, or named IPC pipes are required for clean session teardown.

### 2. Session ID Recycling Hazard
Windows Terminal Server re-uses integer session IDs when a session terminates. If user A logs off without detaching a device (and without auto-cleanup on logoff), the device stays attached to the roothub tagged with user A's `SessionId`. If user B subsequently logs in and is assigned that recycled session ID, user B would inherit full visibility and control over user A's USB peripheral.

### 3. Administrative Override vs. Absolute Lockout
The raw Option A implementation treated all sessions equally, meaning even local administrators (or Session 0 services) were denied access to other users' devices. This caused an operational vulnerability: stuck, faulted, or orphaned devices could never be detached by administrators without rebooting the host. Option A was hardened with `is_admin_request()` (inspecting primary/impersonation tokens for `SeTokenIsAdmin`), allowing administrators to inspect all ports, detach any device, and execute global mass-detach from Session 0.

### 4. Simplified Persistent State Security
Rather than introducing complex per-SID parsing and subkeys in registry `REG_MULTI_SZ` storage (which complicates boot-time reattach before any user logs in), `SET_PERSISTENT` was gated to administrators via `is_admin_request()`. Standard users receive `STATUS_ACCESS_DENIED`, fully neutralizing unprivileged machine-wide registry modification.

### 5. On-Demand Session Isolation vs. Shared Default
By default, attachments remain shared (`isolation::none`) to preserve full backward compatibility with single-user and legacy workflows where any session or service can interact with the attached device. Session isolation is activated on demand via `--isolate=session` (modeled as `enum class isolation { none = 0, session = 1, user = 2 }` to pave the way for future user/SID-based isolation).

### 6. Scope of Windows Sessions (RDP, Fast User Switching, and Multi-User Architecture)
In Windows architecture, "Terminal Services" (Terminal Server) is the OS subsystem underlying Remote Desktop (RDP) and Remote Desktop Services (RDS). All user environments are partitioned by the kernel into integer `SessionId` namespaces:
- **RDP / RDS sessions** on Windows Server and Azure Virtual Desktop each receive distinct interactive `SessionId`s (`Session 2`, `Session 3`, etc.).
- **Standard single-user RDP** connections to Windows 10/11 Pro/Enterprise are allocated dedicated session IDs.
- **Fast User Switching (FUS)** on local workstations assigns distinct session IDs to concurrently logged-on local users on the same physical hardware.
- **Session 0** isolates system services and background daemons from all interactive user sessions.
- **Third-party VDI** solutions (Citrix Virtual Apps/Desktops, VMware Horizon) build directly on the Windows Terminal Services session infrastructure.

Because the driver inspects the caller's session via the core kernel API `IoGetRequestorSessionId`, session isolation operates natively and identically across all of these environments.

