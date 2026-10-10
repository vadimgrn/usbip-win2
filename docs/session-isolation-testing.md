# Testing Session-Based Device Isolation

This document provides step-by-step instructions for testing session-based device isolation in **usbip-win2**.

Session isolation ensures that in multi-user Windows environments—such as Windows Terminal Server / Remote Desktop Services (RDS) or Fast User Switching (FUS)—USB devices attached by one user session remain private, invisible, and untouchable to all other concurrent user sessions.

---

## 1. Overview & Security Architecture

### Isolation Modes
When attaching a remote USB device via `usbip attach`, the client supports the `-i, --isolate` parameter:

| Mode | Flag | Description |
|---|---|---|
| **Shared** (Default) | `-i none` | Device is stamped with `session::invalid`. It is globally visible across all user sessions and services, and can be detached by any user. |
| **Session-Isolated** | `-i session` | Device is stamped with the requestor's Windows Terminal Server `SessionId` (via kernel routine `IoGetRequestorSessionId`). Non-administrative callers in other sessions cannot list, detach, or receive event notifications for the device. |

### Enforced Security Boundaries
1. **List Filtering (`GET_IMPORTED_DEVICES` / `usbip port`)**:
   Standard users only see devices attached in their own session (plus unisolated shared devices). Devices isolated to other sessions are hidden.
2. **Targeted Detach Protection (`PLUGOUT_HARDWARE` / `usbip detach -p <port>`)**:
   Attempting to detach a device owned by another session returns `STATUS_ACCESS_DENIED` (`ERROR_ACCESS_DENIED`, Win32 error code 5).
3. **Scoped Mass Detach (`usbip detach -a`)**:
   Detaching all devices (`port <= 0`) only removes devices owned by the caller's session. Devices owned by other sessions remain attached.
4. **Scoped Attach Cancellation (`STOP_ATTACH_ATTEMPTS` / `usbip attach -X`)**:
   Stopping active attach attempts is scoped to the caller's session; standard users cannot cancel pending attach retries initiated by another session.
5. **Registry Configuration Protection (`SET_PERSISTENT` / `usbip port -s`)**:
   Writing persistent auto-reattach configurations to `HKLM` requires administrative privileges (`STATUS_ACCESS_DENIED` for standard users).
6. **Event Stream Isolation (`IRP_MJ_READ` / GUI notifications)**:
   Driver notifications for device connection and disconnection are delivered only to subscribers whose session matches the device owner.
7. **Administrative Override**:
   Local administrators (`is_admin_request` via token inspection) can list and detach devices across all sessions to recover orphaned devices left behind by disconnected or terminated sessions.

---

## 2. Test Environment Setup & Prerequisites

### 2.1 System Requirements
- **Operating System**:
  - Windows Server (2022 / 2025 Standard or Datacenter) with Remote Desktop Services (RDS), **or**
  - Windows 10 / 11 Pro/Enterprise with Fast User Switching (FUS) or concurrent RDP enabled.
- **Drivers**:
  - `usbip2_ude.sys` (UDE controller driver) and `usbip2_filter.sys` (upper filter driver) installed.
  - Test-signing enabled (`bcdedit /set testsigning on`).
- **Driver Verifier (Recommended for reliability testing)**:
  Enable Driver Verifier on both drivers to detect pool corruption, leakages, or IRQL contract violations:
  ```cmd
  verifier /standard /driver usbip2_ude.sys usbip2_filter.sys
  ```
  *(Requires a system reboot if newly configured).*

### 2.2 Remote USB/IP Server Setup
- A remote Linux host running `usbipd` (e.g. `192.168.1.100:3240` or `172.20.16.75:3240`).
- At least two USB devices exported on the server (plain HID devices like keyboards or mice, or mass storage devices):
  - **Device 1**: exported at bus ID `1-1` (e.g., optical mouse)
  - **Device 2**: exported at bus ID `1-2` (e.g., keyboard)
- *Note on Linux `usbipd` behavior*: On standard Linux USB/IP implementations, when a Windows client detaches a device, the server-side driver may unbind it. If so, re-bind it before running subsequent attach tests:
  ```bash
  usbip bind -b 1-1
  usbip bind -b 1-2
  ```

### 2.3 User Accounts Setup
Create two standard (non-administrator) local users and verify the local Administrator account:

In an elevated PowerShell prompt:
```powershell
net user UserA Password123! /add
net user UserB Password123! /add
```

Verify that neither `UserA` nor `UserB` is a member of the local `Administrators` group:
```powershell
Get-LocalGroupMember -Group "Administrators"
```

### 2.4 Establishing Two Concurrent Sessions
Choose one of the following methods:

- **Method A: Fast User Switching (Local Console on Windows 10/11/Server)**
  1. Log in to the physical console as `UserA`.
  2. Press `Win + L` to lock the workstation, select **Switch User**, and log in as `UserB`.
  3. Both sessions now run concurrently in memory (`UserA` in Session A, `UserB` in Session B). Switch between them via `Win + L`.
- **Method B: Remote Desktop (RDS on Windows Server)**
  1. From a client machine, connect via RDP (`mstsc.exe`) using credentials for `UserA`.
  2. Open a second RDP window and connect using credentials for `UserB`.
- **Method C: Interactive Command Execution (PsExec / Task Scheduler)**
  Launch interactive command shells in each target session:
  ```cmd
  psexec -i <SessionIdA> -u UserA -p Password123! powershell.exe
  psexec -i <SessionIdB> -u UserB -p Password123! powershell.exe
  ```

### 2.5 Inspecting Session IDs
In each user's terminal window, verify identity and session ID:

**In User A's terminal:**
```powershell
whoami
(Get-Process -Id $PID).SessionId
```
*(Assume Session ID is `2`, designated as **Session A**).*

**In User B's terminal:**
```powershell
whoami
(Get-Process -Id $PID).SessionId
```
*(Assume Session ID is `3`, designated as **Session B**).*

**From any command prompt (system overview):**
```cmd
query user
```

---

## 3. Step-by-Step Test Procedure

### Test Case 1: Baseline Verification — Shared Mode (`--isolate=none`)
**Goal:** Verify backward compatibility. When isolation is not requested, devices remain globally accessible across all sessions.

1. **User A (Session A)** attaches Device 1 without isolation:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i none
   ```
2. **User A (Session A)** inspects the port:
   ```cmd
   usbip port
   ```
   **Expected:** Shows Port 01 with `-> isolation: none`.
3. **User B (Session B)** inspects ports:
   ```cmd
   usbip port
   ```
   **Expected:** User B **can see** Device 1 on Port 01 with `-> isolation: none`.
4. **User B (Session B)** detaches Device 1:
   ```cmd
   usbip detach -p 1
   ```
   **Expected:** Detach succeeds (`port 1 is successfully detached`).
5. **User A (Session A)** checks `usbip port`:
   **Expected:** Empty list.

---

### Test Case 2: Session-Isolated Device Attachment & Visibility Filtering
**Goal:** Verify that devices attached with `-i session` are stamped with the caller's session ID and completely hidden from other non-admin sessions.

1. **User A (Session A)** attaches Device 1 with session isolation:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i session
   ```
2. **User A (Session A)** checks `usbip port`:
   ```cmd
   usbip port
   ```
   **Expected Output:**
   ```text
   Imported USB devices
   ====================
   Port 01: device in use at High Speed(480Mbps)
    ...
      -> usbip://<server_ip>:3240/1-1
      ...
      -> mode: zero-copy
      -> isolation: session
   ```
3. **User B (Session B)** checks `usbip port`:
   ```cmd
   usbip port
   ```
   **Expected Output:**
   ```text
   (no output / empty list)
   ```
   *Verification:* User B **cannot see** User A's isolated device.
4. **User B (Session B)** attaches Device 2 with session isolation:
   ```cmd
   usbip attach -r <server_ip> -b 1-2 -i session
   ```
5. **User B (Session B)** checks `usbip port`:
   ```cmd
   usbip port
   ```
   **Expected Output:**
   Shows **only Port 02** (Device 2) with `-> isolation: session`. Port 01 is omitted.
6. **User A (Session A)** checks `usbip port`:
   ```cmd
   usbip port
   ```
   **Expected Output:**
   Shows **only Port 01** (Device 1). Port 02 is omitted.
   *Verification:* Bidirectional cross-session hiding confirmed.

---

### Test Case 3: Unauthorized Cross-Session Detach Prevention
**Goal:** Verify that a user in Session B cannot detach a device owned by Session A.

*Initial State: User A owns Port 01; User B owns Port 02.*

1. **User B (Session B)** attempts to detach User A's device (Port 01):
   ```cmd
   usbip detach -p 1
   ```
   **Expected Output:**
   ```text
   usbip: error: Access is denied.
   ```
   *Verification:* Driver returns `STATUS_ACCESS_DENIED`.
2. **User A (Session A)** checks `usbip port`:
   ```cmd
   usbip port
   ```
   **Expected:** Port 01 remains present, active, and fully operational.
3. **User A (Session A)** attempts to detach User B's device (Port 02):
   ```cmd
   usbip detach -p 2
   ```
   **Expected Output:**
   ```text
   usbip: error: Access is denied.
   ```
4. **User B (Session B)** checks `usbip port`:
   **Expected:** Port 02 remains present and operational.

---

### Test Case 4: Scoped Mass Detach (`detach -a`)
**Goal:** Verify that `usbip detach -a` is scoped to the requestor's session and does not disconnect devices owned by other sessions.

*Initial State: User A owns Port 01; User B owns Port 02.*

1. **User A (Session A)** runs mass detach:
   ```cmd
   usbip detach -a
   ```
   **Expected Output:**
   ```text
   all ports are detached
   ```
2. **User A (Session A)** checks `usbip port`:
   ```cmd
   usbip port
   ```
   **Expected:** Empty list (Port 01 was detached).
3. **User B (Session B)** checks `usbip port`:
   ```cmd
   usbip port
   ```
   **Expected Output:**
   Port 02 **remains attached and untouched**. User A's mass-detach had zero impact on Session B.
4. **User B (Session B)** detaches their own device:
   ```cmd
   usbip detach -p 2
   ```
   **Expected:** Detach succeeds (`port 2 is successfully detached`).

---

### Test Case 5: Scoped Attach Cancellation (`STOP_ATTACH_ATTEMPTS`)
**Goal:** Verify that a user in Session B cannot cancel pending attach retries initiated by Session A.

1. **User A (Session A)** initiates an attach to an invalid or unreachable device on the server (starting background retries):
   ```cmd
   usbip attach -r <server_ip> -b 9-99 -i session
   ```
   *(The command initiates background reconnection attempts).*
2. **User B (Session B)** attempts to cancel all attach attempts:
   ```cmd
   usbip attach -X
   ```
   **Expected Output:**
   Command stops 0 attempts (`count: 0`). User A's background retries continue running.
3. **User A (Session A)** stops attach attempts:
   ```cmd
   usbip attach -X
   ```
   **Expected Output:**
   Successfully stops User A's own pending attach attempt.

---

### Test Case 6: Registry Persistence Protection (`port -s` / `SET_PERSISTENT`)
**Goal:** Verify that non-administrators cannot write to the system-wide persistent device configuration in `HKLM`.

1. **User A (Session A, Standard User)** attaches a device:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i session
   ```
2. **User A (Session A, Standard User)** attempts to stash persistent configuration:
   ```cmd
   usbip port -s
   ```
   **Expected Output:**
   ```text
   usbip: error: Access is denied.
   ```
   *Verification:* Driver enforces `is_admin_request`, preventing unprivileged registry modification.
3. **Administrator** opens an elevated command prompt (Run as Administrator) and executes:
   ```cmd
   usbip port -s
   ```
   **Expected Output:**
   Succeeds (`1 persistent device(s) stashed`).
4. **User A or User B (Standard User)** inspects persistent devices:
   ```cmd
   usbip list -s
   ```
   **Expected Output:**
   Succeeds and displays the persistent configuration. (Read-only queries via `GET_PERSISTENT` are permitted).
5. Clean up the stashed device using the Administrator prompt:
   ```cmd
   usbip detach -p 1
   reg delete "HKLM\SYSTEM\CurrentControlSet\Services\usbip2_ude" /v PersistentDevices /f
   ```

---

### Test Case 7: Administrative Override & Orphaned Device Recovery
**Goal:** Verify that an administrator can inspect and detach devices owned by any session to recover orphaned devices (e.g., when a user disconnects or logs off without detaching).

1. **User A (Session A)** attaches Device 1:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i session
   ```
2. **User A** disconnects from the session or locks the screen.
3. Open an **elevated Administrator prompt** (can be in any session or Session 0):
4. **Administrator** queries ports:
   ```cmd
   usbip port
   ```
   **Expected Output:**
   Administrator sees Port 01 (and all other active ports across all sessions).
5. **Administrator** detaches User A's device:
   ```cmd
   usbip detach -p 1
   ```
   **Expected Output:**
   ```text
   port 1 is successfully detached
   ```
   *Verification:* Administrative override allows freeing stuck devices without requiring a system reboot.
6. *(Optional)* **Session 0 Administrator Mass Detach**:
   From a service or elevated task running in Session 0, executing `usbip detach -a` globally detaches all devices across all sessions.

---

### Test Case 8: Device Handoff Between Sessions
**Goal:** Verify that a device can be transferred from Session A to Session B with clean ownership transfer.

1. **User A (Session A)** attaches Device 1 with `-i session`:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i session
   ```
2. **User A (Session A)** detaches Device 1:
   ```cmd
   usbip detach -p 1
   ```
3. If necessary, re-bind on the Linux server: `usbip bind -b 1-1`.
4. **User B (Session B)** attaches Device 1 with `-i session`:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i session
   ```
5. **User B (Session B)** checks `usbip port`:
   **Expected:** Shows Port 01 with `-> isolation: session`.
6. **User A (Session A)** checks `usbip port`:
   **Expected:** Empty list (User A no longer sees the device).
7. **User A (Session A)** attempts `usbip detach -p 1`:
   **Expected:** Fails with `Access is denied.` Ownership has cleanly transitioned to Session B.
8. Clean up: **User B** detaches Port 01.

---

### Test Case 9: Event Notification Filtering (`IRP_MJ_READ`)
**Goal:** Verify that state change events are delivered only to subscribers in the device's owning session.

1. In **Session A (User A)**, launch the GUI client `wusbip.exe` (or an application reading `IRP_MJ_READ` event stream).
2. In **Session B (User B)**, launch a second instance of `wusbip.exe`.
3. In **Session A (User A)**, attach Device 1 with `-i session`:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i session
   ```
4. **Verification:**
   - Session A's GUI automatically updates to display the newly connected device.
   - Session B's GUI receives **no event notifications** and remains unchanged.
5. In **Session A (User A)**, detach Device 1 (`usbip detach -p 1`):
   - Session A's GUI updates to show the device removed.
   - Session B's GUI receives no events.

---

### Test Case 10: PnP Devnode Isolation & Direct Device Handle Protection
**Goal:** Verify that child device devnodes are hidden from other sessions' Device Manager, and attempts to open raw device handles from other sessions return `STATUS_ACCESS_DENIED`.

1. In **Session A (User A)**, attach a USB device (e.g. smart card, HID, or flash drive) with `-i session`:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i session
   ```
2. **Device Manager Check**:
   - In **Session A**, open `devmgmt.msc`.
   - **Expected:** Device appears under its device class (e.g., Human Interface Devices, Smart Cards, Disk Drives).
   - In **Session B**, open `devmgmt.msc`.
   - **Expected:** Device is **not present** (hidden by `DEVPKEY_Device_SessionId`).
3. **Direct Device Open Check**:
   - In **Session B**, obtain the device interface path or symbolic link.
   - Attempt to open a handle to the device via `CreateFile` (or via a tool/script opening `\\.\USB#...`).
   - **Expected:** Fails with `ERROR_ACCESS_DENIED` (Win32 error 5) via `usbip2_filter.sys`'s `IRP_MJ_CREATE` dispatch filter.

---

### Test Case 11: Storage Volume Drive Letter & DACL Access Isolation
**Goal:** Verify that USB mass storage devices have their drive letters redirected to the owner's session and that foreign sessions (including Administrators) are denied data read/write access.

1. In **Session A (User A)**, attach a USB flash drive with `-i session`:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i session
   ```
   **Output:**
   ```text
   successfully attached to port 1
   storage volume E: isolated to current session (DACL restricted)
   ```
2. **Drive Letter Visibility Check**:
   - In **Session A**, open File Explorer or run `dir E:\`.
   - **Expected:** Drive `E:` exists, directories/files can be read and written normally.
   - In **Session B (User B or Administrator)**, open File Explorer or run `dir E:\`.
   - **Expected:** Drive `E:` does not exist in `This PC`, and `dir E:\` returns `The system cannot find the drive specified.`
3. **Direct Volume Access Check**:
   - In **Session B**, as an elevated **Administrator**, attempt to inspect or open the raw volume `\\.\HarddiskVolumeX` or filesystem root:
     ```cmd
     dir \\.\HarddiskVolumeX
     ```
   - **Expected:** Fails with `Access is denied.` (DACL restricts data plane access strictly to SYSTEM and Session A's Logon SID; Administrators are omitted).
4. In **Session A (User A)**, detach the device:
   ```cmd
   usbip detach -p 1
   ```
   **Expected:** Drive `E:` session mapping is cleanly removed.

---

### Test Case 12: User-Based Device Isolation (`-i user`) Across Accounts & Reconnects
**Goal:** Verify that a device attached with `-i user` is bound strictly to the owner's Windows User SID (`TokenUser`). Verify that another user account cannot access or detach the device, and that ownership persists across RDP disconnect/reconnect cycles where `SessionId` may change.

1. **User A (Alice)** attaches a USB device with `-i user`:
   ```cmd
   usbip attach -r <server_ip> -b 1-1 -i user
   ```
   **Output:**
   ```text
   successfully attached to port 1
   storage volume E: isolated to current session (DACL restricted)
   ```
2. **User B (Bob)** checks device list:
   ```cmd
   usbip port
   ```
   **Expected:** Port 1 is hidden from Bob's list.
3. **User B (Bob)** attempts to detach Alice's device:
   ```cmd
   usbip detach -p 1
   ```
   **Expected Output:**
   ```text
   usbip: error: Access is denied.
   ```
4. **User B (Bob)** attempts to access drive `E:\`:
   ```cmd
   dir E:\
   ```
   **Expected:** `The system cannot find the drive specified.`
5. **Foreign Administrator** attempts to open the device PDO or volume:
   - Upper filter FiDO intercepts `IRP_MJ_CREATE` and verifies caller SID against Alice's SID.
   - **Expected:** Driver returns `STATUS_ACCESS_DENIED`.
6. **Alice disconnects and reconnects** (session reconnect where RDP may assign a new `SessionId`):
   - Alice runs `usbip port`.
   - **Expected:** Port 1 remains listed and operational for Alice because ownership is bound to Alice's immutable User SID rather than ephemeral `SessionId`.
7. **User A (Alice)** detaches the device:
   ```cmd
   usbip detach -p 1
   ```
   **Expected Output:**
   ```text
   port 1 is successfully detached
   ```

---

## 4. Test Matrix Summary

| Test Case | Actor / Context | Action | Target Port / Device | Expected Outcome | Security Contract |
|---|---|---|---|---|---|
| **1. Shared Baseline** | User A | `attach -i none` | Port 1 | Visible to all sessions | Backward compatibility |
| | User B | `detach -p 1` | Port 1 | Detach succeeds | Unisolated devices are shared |
| **2. Isolated Visibility** | User A | `attach -i session` | Port 1 | Visible in Session A only | Cross-session privacy |
| | User B | `port` | Port 1 | Empty (hidden) | Information disclosure prevention |
| | User B | `attach -i session` | Port 2 | Visible in Session B only | Bidirectional isolation |
| | User A | `port` | Port 2 | Port 1 only (Port 2 hidden) | Isolation maintained |
| **3. Unauthorized Detach** | User B | `detach -p 1` | User A's Port 1 | `Access is denied` (5) | Cross-session control blocked |
| | User A | `port` | Port 1 | Device intact & functional | Device integrity preserved |
| | User A | `detach -p 2` | User B's Port 2 | `Access is denied` (5) | Symmetric protection |
| **4. Scoped Mass Detach** | User A | `detach -a` | All ports | Detaches Port 1 only | Scoped to caller's session |
| | User B | `port` | Port 2 | Port 2 intact & active | Other sessions' devices protected |
| **5. Cancel Attempts** | User B | `attach -X` | Active retries | Cancels 0 attempts | Cannot cancel another session's retries |
| | User A | `attach -X` | Active retries | Cancels User A's retries | Caller controls own retries |
| **6. Registry Protection** | Standard User | `port -s` | Persistent state | `Access is denied` (5) | Unprivileged `HKLM` write blocked |
| | Administrator | `port -s` | Persistent state | Succeeds | Privileged configuration allowed |
| | Standard User | `list -s` | Persistent state | Succeeds (read-only) | Read-only configuration inspection |
| **7. Admin Override** | Administrator | `port` | All ports | Lists all sessions' devices | Global administrative visibility |
| | Administrator | `detach -p 1` | User A's Port 1 | Detach succeeds | Recovery of orphaned devices |
| **8. Device Handoff** | User B | `attach -i session` | Port 1 | Sits in Session B | Seamless transfer of ownership |
| | User A | `detach -p 1` | Port 1 | `Access is denied` (5) | Previous owner loses control |
| **9. Event Filtering** | Subscribers | Attach/detach | Event stream | Events sent to owner only | No cross-session event leakage |
| **10. Devnode & IRP Gate** | User B | `devmgmt.msc` / `CreateFile` | User A's Device | Hidden in devmgmt; `CreateFile` returns `Access is denied` | PnP and IRP isolation |
| **11. Storage Plane Isolation** | User B / Admin B | File Explorer / `dir E:\` | User A's USB Drive | Drive letter missing; volume read returns `Access is denied` | Storage volume & DACL isolation |
| **12. User-Based Isolation** | User B / Admin B | `port`, `detach`, `CreateFile` | User A's `-i user` Device | Hidden in `port`; `detach` & `CreateFile` return `Access is denied` | SID-based device isolation |

---

## 5. Diagnostics & Troubleshooting

### 5.1 Verifying Process Session IDs
To verify which session ID a process is running in:
```powershell
(Get-Process -Id $PID).SessionId
```
To check token privileges (confirming standard user vs. administrator):
```cmd
whoami /priv
whoami /groups
```

### 5.2 Enabling Real-Time Driver Tracing (ETW / WPP)
`usbip2_ude.sys` logs detailed session and security checks via WPP Software Tracing.

1. Start tracing in an elevated prompt using `tracelog`:
   ```cmd
   tracelog -start USBIP -guid #0cb7883d-3df7-4b72-a6fc-6e93bcff6b14 -flags 0x7fffffff -level 5 -rt
   ```
2. Format and view traces in real time:
   ```cmd
   tracefmt -display -rt USBIP
   ```
3. Look for trace messages containing:
   - `get_requestor_session_id`
   - `session <N>, admin <bool>`
   - `STATUS_ACCESS_DENIED`
4. Stop tracing:
   ```cmd
   tracelog -stop USBIP
   ```

### 5.3 Common Issues
- **`usbip: error: Access is denied` on `detach`**:
  Expected behavior when attempting to detach a device owned by another session. If unexpected, verify that the caller's session ID matches the attaching session ID using `(Get-Process -Id $PID).SessionId`.
- **`usbip: error: Device busy (already exported)` on server**:
  Benign race condition with Linux `usbipd` when reattaching immediately. Run `usbip port` on the client to confirm whether the device enumerated.
- **Orphaned devices after user logoff**:
  If a user logs off without detaching an isolated device, run `usbip detach -p <port>` from an elevated Administrator prompt to free the hub port.
