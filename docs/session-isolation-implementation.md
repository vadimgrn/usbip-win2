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
| Owner field | `context.h` | `device_ctx.session_id` (owner) and `fileobject_ctx.session_id` (subscriber); `invalid_session_id` constant |
| Session of caller | `vhci_ioctl.cpp` | `get_requestor_session_id()` helper (local `extern "C"` prototype for `IoGetRequestorSessionId`, which lives in `ntifs.h` and must not be pulled into a KMDF TU) |
| Capture owner at attach | `vhci_ioctl.cpp` (`plugin_hardware`, `connected`), `device.cpp` (`device::create` inits to `invalid_session_id`) | stamps `device_ctx.session_id` |
| **Detach control** | `vhci_ioctl.cpp` (`plugout_hardware`) | single-port detach returns `STATUS_ACCESS_DENIED` if owned by another session; the `port <= 0` mass-detach is scoped to the caller's own devices via `detach_all_devices(..., session_id)` (`vhci.cpp`) |
| **List** | `vhci_ioctl.cpp` (`get_imported_devices`) | skips devices not owned by the caller |
| **Event stream** | `vhci.cpp` (`process_event`, `device_state_changed` gains a `session_id`), `vhci.h` | a state change is delivered only to subscribers in the owning session; `device_read` binds a subscription to the caller's session and rejects a cross-session read on a shared handle (`STATUS_ACCESS_DENIED`) |
| **Owner across auto-reattach** | `persistent.cpp/.h` (`attach_ctx.session_id`, `start_attach_attempts(..., session_id)`, new `find_attach_session`), `vhci_ioctl.cpp`, `device.cpp` | a (re)attach travels to the driver via a `target_self` IOCTL that erases the requestor session; the owner is preserved in the pending attach request and recovered by `location_hash` in `plugin_hardware`, so a network blip does not orphan the user's device |

Net: ~127 insertions / ~35 deletions across 7 files. No new components, no change to the install
model, the device object, or the SDDL.

## What this closes (the Phase 4 functional matrix)

- B cannot **list** A's devices (`GET_IMPORTED_DEVICES` filtered).
- B cannot **detach** A's device (`STATUS_ACCESS_DENIED`); B's `port <= 0` mass-detach only removes B's own.
- B cannot **see** A's device state changes on the event stream; a leaked read handle is rejected.
- A leaked control handle is rejected because the check is on the *current* requestor, not the opener.
- Auto-reattach keeps the original owner.

## Known residuals (deferred, documented on purpose)

These are **not** gated in this increment and should be a follow-up increment or an explicit decision:

1. **`SET_PERSISTENT` / `GET_PERSISTENT`** — still machine-wide; any session can read or overwrite the
   `HKLM\...\usbip2_ude\State\PersistentDevices` list. Per-SID persistent state is semantically tricky
   because boot-time auto-reattach runs with no logged-on session; needs a design decision.
2. **Boot-persisted devices** now attach with `invalid_session_id`, so they are **not visible in any
   live session** (previously machine-wide visible). This is the secure default under isolation but
   disables the persistent feature's visibility until (1) is designed.
3. **`STOP_ATTACH_ATTEMPTS`** — not scoped; any session can cancel attach attempts (including the empty
   "stop all"). Deliberately left alone to avoid touching the delicate, DoS-sensitive reattach
   cancellation path; severity is low (transient attach *retries*, not attached devices).
4. **`IOCTL_USB_USER_REQUEST`** and other standard USB IOCTLs handled by `udecx` are not filtered — they
   are Windows USB queries against the shared controller.
5. **Roothub port exhaustion** — the port array is a shared pool; one session can exhaust it and deny
   attach to others (a shared-resource DoS, not a confidentiality/integrity breach).

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
