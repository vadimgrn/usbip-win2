# Session-Isolation Test Results (Phase 4)

Functional validation of the Option-A session-isolation boundary (see
`session-isolation-implementation.md`). All checks passed. The one deferred residual (machine-wide
persistent list) was confirmed to behave exactly as documented.

## Environment

| | |
|---|---|
| Target | Windows Server 2025 Standard (build 26100.32230), KMDF/UDE |
| Drivers | `usbip2_ude.sys` + `usbip2_filter.sys`, Release, test-signed (built 2026-07-23) |
| Instrumentation | **Driver Verifier** `/standard` (Special Pool + I/O + pool tracking + DDI + WDF) on **both** drivers, active for the whole run |
| USB/IP server | Linux (Debian, usbip-utils 2.0) at 172.20.16.75:3240 |
| Test devices | HP Optical Mouse `03f0:094a` (busid 1-4); HP 125 composite keyboard `03f0:564a` (busid 3-1) — both plain HID (interrupt + standard control), chosen to avoid the unrelated filter bug (see `upstream-issue-180-report.md`) |
| Sessions | Session 0 = admin over OpenSSH (services); Session 1 = console (Administrator); Session 2 = RDP `testA`; Session 3 = RDP `testB` |

Commands were executed inside each RDP user's real TS session via an interactive-token scheduled task
(PsExec was unavailable), confirmed each time by printing `whoami` + the process `SessionId`.

## Results

### Regression / smoke test (single session)
Attach mouse (Session 0) → enumerates as HID, `usbip port` shows it, controller stays healthy, no
crash. Attach composite keyboard likewise (5 HID interfaces). Baseline OK.

### Isolation matrix — Session 0 (admin) owns both devices, testA is a separate session
| Check | Expected | Result |
|---|---|---|
| testA `usbip port` | cannot see Session 0's devices | ✅ empty list |
| testA `detach -p 1` / `-p 2` (Session 0's) | refused | ✅ Access Denied, both |
| Devices after refused detaches | intact | ✅ both still present in Session 0 |

### Isolation matrix — two RDP users (the production scenario)
State: **testA (sess 2) owns the mouse (port 2)**, **testB (sess 3) owns the keyboard (port 1)**.

| # | Check | Expected | Result |
|---|---|---|---|
| 1 | testA `usbip port` | only its mouse | ✅ shows port 2 only |
| 2 | testB `usbip port` | only its keyboard | ✅ shows port 1 only |
| 3 | testA `detach -p 1` (testB's) | refused | ✅ Access Denied |
| 4 | testB `detach -p 2` (testA's) | refused | ✅ Access Denied |
| 5 | Devices survive 3–4 | yes | ✅ both intact |
| 6 | Session-0 admin `detach -p 1`/`-p 2` | refused | ✅ Access Denied (no privilege bypass) |
| 7 | testA `detach -a` (mass-detach) | drops only testA's | ✅ testA emptied, **testB's keyboard survived** |

The positive direction is covered too: when a device is handed from one owner to another, the new
owner's `usbip port` immediately shows it and the previous owner's no longer does — filtering works in
both directions, not just "deny everything."

### Residual confirmed (not a regression — documented Option-A limitation)
testB `port -s` (stash keyboard as persistent) → testA `list -s` shows `172.20.16.75:3240/3-1`. The
persistent/auto-reattach list is machine-wide, **not** session-filtered. Cleaned up after the test.

## Notes & gotchas observed

- The boundary is **purely session-scoped**: an administrator in a *different* TS session is denied a
  user's device just like any other session. Elevation does not bypass it (the check is
  `IoGetRequestorSessionId` per IRP, independent of token privilege).
- **No crash the entire run** — attach, console login, live HID input, two concurrent RDP users — with
  Driver Verifier Special Pool armed. The pre-existing filter pool-corruption bug (issue #180) did not
  reproduce under Verifier, consistent with a timing-sensitive race; it is orthogonal to this feature.
- usbipd quirk on this server: a device is **unbound shortly after a client detaches**, so handing a
  device between sessions needs `usbip bind -b <busid>` again before the next attach. "Device busy
  (already exported)" on attach is a benign server re-export race — trust `usbip port`.
