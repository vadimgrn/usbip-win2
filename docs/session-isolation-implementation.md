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

Every attached device (`UDECXUSBDEVICE` / `device_ctx`) is tagged with an owning session id
(`IoGetRequestorSessionId` of the attach request). Each user-facing operation reads the **current
requestor's** session (not a value cached at handle-open, so a handle leaked from session A to B is
still rejected when B uses it) and compares it to the target device's owner. Mismatch → the device is
invisible / the operation is denied.

`invalid_session_id` (`MAXULONG`) is an owner that never matches any real session, so an un-stamped or
kernel-originated device denies everyone by default.

## Changes (drivers/ude)

| Area | File | Change |
|---|---|---|
| Owner field | `context.h`, `consts.h` | `device_ctx.session_id` (owner) and `fileobject_ctx.session_id` (subscriber); central `invalid_session_id` constant |
| Session of caller | `vhci_ioctl.cpp` | `get_requestor_session_id()` and `is_admin_request()` helpers (token security and session inspection via kernel exports) |
| Capture owner at attach | `vhci_ioctl.cpp` (`plugin_hardware`, `connected`), `device.cpp` (`device::create` inits to `invalid_session_id`) | stamps `device_ctx.session_id`; `find_attach_session` only called if requestor is `invalid_session_id` to prevent cross-session attach hijacking |
| **Detach control** | `vhci_ioctl.cpp` (`plugout_hardware`), `vhci.cpp` (`detach_all_devices`) | single-port detach returns `STATUS_ACCESS_DENIED` across sessions unless caller is administrator; mass-detach (`port <= 0`) is scoped to caller's own session, while Session 0 admin can detach all |
| **List** | `vhci_ioctl.cpp` (`get_imported_devices`) | skips devices not owned by caller unless caller is administrator (admins can inspect all ports) |
| **Event stream** | `vhci.cpp` (`process_event`, `device_state_changed`, `replay_plugged_devices`), `vhci.h` | state changes and replays are delivered only to subscribers in the owning session; `device_read` rejects cross-session reads (`STATUS_ACCESS_DENIED`) |
| **Owner across auto-reattach** | `persistent.cpp/.h` (`attach_ctx.session_id`, `start_attach_attempts`, `find_attach_session`), `vhci_ioctl.cpp`, `device.cpp` | owner preserved across `target_self` loopback and recovered by `location_hash` |
| **Scoped cancellation** | `persistent.cpp/.h` (`stop_attach_attempts`, `reattach_req_remove`), `vhci_ioctl.cpp` | `STOP_ATTACH_ATTEMPTS` is scoped to caller's session (admins can cancel globally), preventing cross-session DoS |
| **Persistent state write** | `vhci_ioctl.cpp` (`set_persistent`) | `SET_PERSISTENT` requires administrator privilege (`is_admin_request`), preventing unprivileged HKLM modification |

Net: Option A boundary enforced with administrator override, scoped cancellation, and protected registry writes.

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
4. **Session Logoff Handling** — without a dedicated service/agent tracking session logoff events (`WTS_SESSION_LOGOFF`), devices remain attached when a user logs off until manually detached or reallocated. Administrators can detach any orphaned device via single-port detach or Session 0 mass-detach.

## Validation (done — 2026-07-24)

Full results and command evidence: **`docs/session-isolation-test-results.md`**. Summary:

1. **Load smoke test** — passed. Controller enumerates; single-session attach → `usbip port` → detach
   works (HP mouse + HP composite keyboard over USB/IP, both plain-HID, no crash).
2. **Phase 4 isolation matrix** — passed with two RDP users (`testA` sess 2, `testB` sess 3) under
   Driver Verifier Special Pool on both drivers: cross-session list is filtered, cross-session detach
   (user→user *and* admin→user) is `ACCESS_DENIED` with the device surviving, and `detach -a`
   (mass-detach) only affects the caller's own session. Residual (1) confirmed live: the persistent
   list is machine-wide (testB's stash is visible to testA).

Not exercised at runtime (verified by code review): the event-stream cross-session read guard in
`device_read`, and the leaked-handle current-requestor check (both are on the same
`get_requestor_session_id`-per-IRP path as the validated `GET_IMPORTED_DEVICES`/detach checks).
