# Session-Isolation Implementation (Phase 3, increment 1 — Option A)

Implements the **Option A** boundary from `docs/session-isolation-findings.md`: one shared driver,
per-request access control keyed on the Terminal Server session that attached each device. The design
goal was the smallest change that closes the boundary, staying close to the tested upstream driver.
Device-tree visibility is intentionally *not* addressed (blocked-but-visible is acceptable per the
Phase 2 decision).

**Status:** compiles clean and test-signs (`usbip2_ude.sys` + `.cat`, WDK NuGet 10.0.28000, signability
test clean). **Functionally validated** on Windows Server 2025 with two RDP users under Driver
Verifier — see `docs/session-isolation-test-results.md` for the full matrix and evidence.

## Model

Attachments are **shared by default** (`isolation::none`), maintaining full backward compatibility with single-user and legacy workflows where any session or service can interact with an attached device.

Terminal Server session isolation is activated **on demand** via the `--isolate=session` CLI flag or SDK `args.iso_mode = usbip::isolation::session`.

The model uses an extensible enumeration designed to accommodate future user/SID-based isolation:

```cpp
enum class isolation : unsigned char {
        none    = 0,
        session = 1,
        user    = 2, // reserved for future user/SID isolation
};
```

Isolation mode is encoded in bits 2–3 of the attach flags on the wire and stored in the persistent registry configuration (`REG_MULTI_SZ`).

When a device is attached:
- **Shared (`isolation::none`, default)**:
  - `device_ctx.iso_mode` is set to `isolation::none`.
  - `device_ctx.session_id` is set to `invalid_session_id` (`MAXULONG`).
  - `device_ctx::is_session_isolated()` returns `false`.
  - The device is visible to all sessions via `GET_IMPORTED_DEVICES`, detachable by any session via single-port `PLUGOUT_HARDWARE`, and its state events are broadcast machine-wide to all subscribers.
- **Session-isolated (`isolation::session`)**:
  - `device_ctx.iso_mode` is set to `isolation::session`.
  - `device_ctx.session_id` is stamped with the requestor's Terminal Server session id (`IoGetRequestorSessionId`).
  - `device_ctx::is_session_isolated()` returns `true`.
  - User-facing operations check the **current requestor's** session (not a cached handle-open session):
    - Mismatch → `GET_IMPORTED_DEVICES` hides the device, single-port `PLUGOUT_HARDWARE` returns `STATUS_ACCESS_DENIED`, and state events / replays are filtered out.
    - Local administrators (`is_admin_request`) bypass session restrictions to inspect and detach any port.

### What Windows Sessions Actually Are (Scope of Session Isolation)

In Windows, **"Terminal Services"** (or **Terminal Server**) is the underlying operating system architecture name for what is marketed as **Remote Desktop (RDP)** and **Remote Desktop Services (RDS)**. 

The Windows kernel organizes **all** user execution environments into integer-based `SessionId` containers, queried via `IoGetRequestorSessionId`:

| Session Context | Description | Session ID | Handled by `--isolate=session` |
|---|---|---|---|
| **RDP Sessions (RDS / Terminal Server)** | Multi-user Remote Desktop on Windows Server (RDSH), Azure Virtual Desktop (AVD), and multi-session Windows. | `Session 2`, `Session 3`, etc. | **Yes** — Each remote user has their own distinct session ID. |
| **Standard RDP (Windows 10/11)** | Standard remote desktop connection to a Windows 10/11 Pro/Enterprise workstation. | `Session 1` (or next allocated) | **Yes** — The remote user has their own session. |
| **Fast User Switching (FUS)** | Multiple local users logged into the *same physical machine* concurrently (e.g. User A switches to User B). | User A = `Session 1`<br>User B = `Session 2` | **Yes** — Even locally on the same physical box, User A cannot see or detach User B's isolated devices. |
| **Session 0** | Non-interactive system services, background daemons, and driver helper processes. | `Session 0` | **Yes** — Isolated from all interactive user sessions. |
| **Citrix / VMware Horizon / VDI** | Third-party virtual desktop and app streaming solutions running on Windows. | `Session N` | **Yes** — Built directly on top of the Windows Terminal Services session manager. |

Because `IoGetRequestorSessionId` extracts the session ID directly from the IRP requestor, session isolation works uniformly across all RDP sessions, local Fast User Switching, and multi-session RDS environments.

#### Ephemeral Sessions vs. Future User/SID Isolation
Session IDs are **ephemeral** and bound to a specific logon session lifetime. If a user disconnects and reconnects (potentially receiving a new `SessionId`), or logs in simultaneously across both console and RDP, session isolation treats them as separate environments. The extensible `enum class isolation` includes `isolation::user` so that future enhancements can optionally bind device attachments to the caller's Windows Security Identifier (SID) from their security token.

## Changes Across the Stack

| Component | File | Change |
|---|---|---|
| Wire & Protocol | `include/usbip/vhci.h` | `enum class isolation` in `namespace usbip::vhci`; attach flags pack isolation into bits 2–3 (`pack_attach_flags`/`unpack_attach_flags`); added `isolation iso_mode` to `imported_device_properties` and `plugin_hardware` |
| Public SDK | `userspace/libusbip/vhci.h`, `persistent.h` | `enum class isolation` in `namespace usbip`; added `iso_mode` to `imported_device`, `persistent_device`, and `device_attach_args` |
| SDK Implementation | `userspace/libusbip/src/vhci.cpp`, `persistent.cpp` | Propagates `iso_mode` during attach and packs/unpacks `iso_mode` in `REG_MULTI_SZ` persistence strings |
| CLI Interface | `userspace/usbip/usbip.cpp`, `attach.cpp` | Added `-i,--isolate [none\|session]` option (defaults to `none`); passes selection to attach command |
| CLI Display & Formatting | `userspace/usbip/port.cpp`, `strings.cpp`, `strings.h` | Outputs `-> isolation: <mode>` in `usbip port`; formats isolation mode for console and persistent serialization |
| Compile Test | `userspace/libusbip_check/main.cpp` | Validates C++17 compatibility with `isolation` enum in `device_attach_args` |
| Driver Context | `drivers/ude/context.h`, `context.cpp` | `device_ctx.iso_mode`; `iso_mode()` and `is_session_isolated()` helpers; `create_device_ctx_ext` initializes `iso_mode` and stamps session only if isolated |
| Driver Persistence | `drivers/ude/persistent.cpp`, `persistent.h` | Unpacks `iso_mode` from persistent records; propagates across auto-reattach loopback |
| Driver Detach & Event Filtering | `drivers/ude/vhci.cpp` | `detach_all_devices`, `process_event`, and `replay_plugged_devices` skip session checks for unisolated devices (`!dc->is_session_isolated()`) |
| Driver IOCTL Dispatch | `drivers/ude/vhci_ioctl.cpp` | `plugin_hardware` captures session only if `iso_mode == isolation::session`; `get_imported_devices` and `plugout_hardware` gate access only if `is_session_isolated()` |

Net: Option A boundary enforced on demand with administrator override, scoped cancellation, and protected registry writes.

## What this closes (the Phase 4 functional matrix)

- B cannot **list** A's devices (`GET_IMPORTED_DEVICES` filtered; administrators can list all).
- B cannot **detach** A's device (`STATUS_ACCESS_DENIED`; administrators can detach any stuck port).
- B cannot **cancel** A's attach attempts (`STOP_ATTACH_ATTEMPTS` scoped to caller's session).
- B cannot **overwrite** machine-wide persistent configuration (`SET_PERSISTENT` requires administrator privilege).
- B cannot **see** A's device state changes on the event stream; a leaked read handle is rejected.
- A leaked control handle is rejected because the check is on the *current* requestor, not the opener.
- Auto-reattach keeps the original owner without allowing cross-session hijacking.

## Known residuals (deferred, documented on purpose)

1. **`GET_PERSISTENT`** — still machine-wide read; any session can inspect the boot-time persistent list.
2. **`IOCTL_USB_USER_REQUEST`** and other standard USB IOCTLs handled by `udecx` are not filtered — they
   are Windows USB queries against the shared controller.
3. **Roothub port exhaustion** — the port array is a shared pool; one session can exhaust it and deny
   attach to others (a shared-resource DoS, not a confidentiality/integrity breach).
4. **Session Logoff Handling & Session ID Recycling** — 
   - *Risk*: When a Terminal Server user logs off, Windows terminates user-mode processes, but kernel objects (`UDECXUSBDEVICE`) remain attached to the root hub with their original `session_id`. If a subsequent user later logs in and Terminal Services re-allocates that same recycled `SessionId`, the new user would acquire ownership of the previous user's device. Furthermore, until detached, abandoned devices consume roothub ports.
   - *Operational Mitigation (Implemented)*: Administrators can inspect all attached devices via `GET_IMPORTED_DEVICES` and detach orphaned devices across sessions via single-port detach or Session 0 mass-detach (`detach_all_devices(vhci, true, invalid_session_id)`).
   - *Automated Kernel Mitigation (Architectural Path)*: Windows provides the kernel-native `IoRegisterContainerNotification` API (`IoSessionStateNotification` class with `IO_SESSION_STATE_NOTIFICATION`). This allows `usbip2_ude.sys` to register an `IO_SESSION_NOTIFICATION_FUNCTION` directly in kernel mode to receive `IO_SESSION_STATE_LOGOFF_EVENT` and `IO_SESSION_STATE_TERMINATION_EVENT` callbacks. Querying `IoGetContainerInformation(IoSessionStateInformation, ...)` retrieves the terminating `SessionId`, enabling purely in-driver automatic detachment via `detach_all_devices(vhci, true, session_id)` without requiring any user-mode service or helper process.

## Validation & Evolution

Full empirical results and command evidence from the initial baseline test run: **`docs/session-isolation-test-results.md`**.

### Baseline Validation (2026-07-24)
- **Load smoke test**: Passed. Controller enumerates; single-session attach → `usbip port` → detach works (HP mouse + HP composite keyboard over USB/IP, both plain-HID, no crash).
- **Isolation matrix**: Passed with two RDP users (`testA` sess 2, `testB` sess 3) under Driver Verifier Special Pool on both drivers (`usbip2_ude.sys` + `usbip2_filter.sys`). Cross-session list was filtered, cross-session user-to-user detach returned `STATUS_ACCESS_DENIED`, and `detach -a` only detached the caller's own devices.

### Hardened Production Model (Commit `b637b8f`)
Following baseline testing, code review and security analysis identified operational and security issues with the strict session-only model:
1. **Administrative Override**: Baseline check 6 denied Session-0 admin detach. This caused administrative lockout where stuck/orphaned devices could never be recovered without a host reboot. The hardened driver inspects caller tokens (`is_admin_request`), allowing administrators to view all ports, detach any port across sessions, and execute global mass-detach from Session 0.
2. **Registry Configuration Protection**: Baseline permitted any user to call `SET_PERSISTENT`. The hardened driver restricts `SET_PERSISTENT` to administrators (`STATUS_ACCESS_DENIED` for standard users).
3. **Cancellation DoS Mitigation**: Baseline permitted any session to cancel attach retries machine-wide via `STOP_ATTACH_ATTEMPTS`. The hardened driver scopes cancellation to the requestor's session.
4. **Attach Race Protection**: Baseline allowed pending reattaches to potentially stamp their session onto new attach requests. The hardened driver gates session lookup strictly to requests originating without a valid session.

