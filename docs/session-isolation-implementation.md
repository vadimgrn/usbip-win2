# Device Isolation Architecture & Implementation (Session- and User-Based)

Implements complete, multi-layered device and storage volume isolation for Windows Remote Desktop Services (RDS), multi-user environments, and shared workstations. Devices attached with isolation are invisible, inaccessible, and unusable by foreign user sessions or foreign user accounts.

**Isolation Modes Supported:**
1. **Session-Based (`-i session`, `isolation::session`)**: Binds the device to the attaching Terminal Server `SessionId`. Scopes device visibility, PnP devnode enumeration, driver handle access, and drive letters to the interactive logon session.
2. **User-Based (`-i user`, `isolation::user`)**: Binds the device to the caller's immutable Windows Security Identifier (SID from `TokenUser`). Persists ownership across session reconnects, Fast User Switching, and Terminal Services session reassignments. Denies access to foreign users and foreign administrators.

**Status:** Compiles clean and test-signs across both **x64** and **ARM64** platforms (`usbip2_ude.sys` + `usbip2_filter.sys` + `libusbip.dll` + `usbip.exe` + `wusbip.exe` + Inno Setup installers). Compile-time compatibility verified via `libusbip_check` under C++17.

---

## 1. Architectural Model & Defense-in-Depth

The solution employs a four-layer defense-in-depth architecture covering the control plane, PnP devnode hierarchy, device IRP dispatch, and the storage data plane:

```
+-----------------------------------------------------------------------------------+
| 1. Control Plane (usbip2_ude.sys)                                                 |
|    - plugin_hardware records caller SessionId and User SID (TokenUser)            |
|    - INTERNAL_GET_PORT_ISOLATION provides port isolation state to filters         |
|    - GET_IMPORTED_DEVICES, PLUGOUT_HARDWARE, and event stream filter by owner     |
|    - Administrators retain management & recovery override for detach/list        |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 2. PnP Devnode Plane (usbip2_filter.sys)                                          |
|    - Upper filter on USB\ROOT_HUB30 queries port isolation via internal IOCTL     |
|    - Synchronously stamps DEVPKEY_Device_SessionId on child USB PDOs              |
|    - Hides devnodes from Device Manager and PnP enumerations in foreign sessions  |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 3. Device IRP Security Plane (usbip2_filter.sys)                                  |
|    - Upper filter FiDO enforces FILE_DEVICE_SECURE_OPEN across entire namespace   |
|    - IRP_MJ_CREATE (create) checks SessionId or User SID against owner             |
|    - Rejects foreign users/sessions (standard & admin) with STATUS_ACCESS_DENIED  |
|    - Permits Session 0 / SYSTEM (class installers, MountMgr) & KernelMode callers |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 4. Storage & Volume Data Plane (libusbip, usbip.exe, wusbip.exe)                  |
|    - Drive Letter Redirection: Removes global \GLOBAL??\X: mount points and       |
|      maps drive letters inside caller's \Sessions\<Id>\DosDevices\<LogonId>\X:    |
|    - Volume DACL Hardening: Protects volume device object & filesystem root with  |
|      SDDL D:P(A;;GA;;;SY)(A;;GA;;;<LogonSID>)(A;;GA;;;<UserSID>)                  |
|    - Explicit Default-Deny: Administrators (BA) omitted from data plane DACL      |
+-----------------------------------------------------------------------------------+
```

---

## 2. Comparison: Session Isolation vs. User Isolation

| Feature / Dimension | Session Isolation (`-i session`) | User-Based Isolation (`-i user`) |
|---|---|---|
| **Identity Anchor** | Ephemeral `SessionId` (Terminal Server) | Immutable User SID (`TokenUser` via `SeQueryInformationToken`) |
| **Session Disconnect / Reconnect** | If RDS reassigns a new session ID, device ownership is bound to the old ID | Retains ownership regardless of session ID reassignment |
| **Fast User Switching** | Different session ID per logged-in user | Bound to user SID; invisible/inaccessible across accounts |
| **Console `runas` / Service Impersonation** | Shares session ID with console user | Distinguishes callers by effective user token / impersonation token |
| **PnP Devnode Stamping** | Stamps `DEVPKEY_Device_SessionId` | Stamps `DEVPKEY_Device_SessionId` when session ID is known |
| **Upper Filter Gate (`IRP_MJ_CREATE`)** | Matches `IoGetRequestorSessionId(irp) == dev.session_id` | Matches `libdrv::is_caller_sid(irp, dev.owner_sid)` |
| **Kernel & SYSTEM Whitelist** | KernelMode, Session 0, `MAXULONG` | KernelMode, `SeLocalSystemSid` (`libdrv::is_system_request`) |
| **Foreign Administrator Access** | Management: allowed; Data plane: denied | Management: allowed; Data plane: denied |

---

## 3. Administrative Role Separation: Management vs. Data Plane

To satisfy the strict security requirement where administrators in foreign sessions or foreign accounts must not read or write user data:
- **Control Plane (Management)**: Local administrators can list ports and detach stuck or orphaned devices across sessions/users to prevent denial-of-service and roothub port exhaustion after unexpected user logoffs or machine disconnects.
- **Data Plane (Read/Write)**: Foreign administrators are strictly denied access to the device handles (`IRP_MJ_CREATE` rejects non-owning callers with `STATUS_ACCESS_DENIED`) and storage data (the volume device DACL omits `Administrators`, enforcing default-deny under the Windows Security Reference Monitor).

---

## 4. Key Implementation Details

### 4.1 Zero-Allocation Kernel SID Representation (`sid_data`)
To eliminate pool allocations, fragmentation, and potential leaks in kernel I/O paths, `sid_data` embeds a fixed buffer matching `SECURITY_MAX_SID_SIZE` (68 bytes):
```cpp
enum { max_sid_size = 68 }; // SECURITY_MAX_SID_SIZE

struct sid_data
{
        UCHAR data[max_sid_size];
        ULONG length;
};
```

### 4.2 Shared Kernel Security Library (`drivers/libdrv/security.h`)
Encapsulates token extraction and SID comparison using pure NT kernel APIs (`PsReferenceImpersonationToken`, `PsReferencePrimaryToken`, `SeQueryInformationToken(TokenUser)`, `RtlEqualSid`):
- `get_requestor_sid(irp, sid)`: Inspects thread impersonation token first, falling back to process primary token.
- `is_caller_sid(irp, expected_sid)`: Validates caller SID against expected SID, always passing `KernelMode`.
- `is_system_request(irp)`: Validates caller against `SeLocalSystemSid` (or `KernelMode`).
- `is_admin_request(irp)`: Validates caller against `SeAliasAdminsSid` (or `KernelMode`).
- `equal_sid(a, b)`: Performs length check and `RtlEqualSid`.

### 4.3 Upper Filter Dispatch Gate (`drivers/ude_filter/driver.cpp`)
Intercepts `IRP_MJ_CREATE` before requests reach underlying device stacks:
- **Session Mode**: Checks `req_session == dev.session_id || mode == KernelMode || req_session == 0 || req_session == MAXULONG`.
- **User Mode**: Checks `libdrv::is_caller_sid(irp, dev.owner_sid) || libdrv::is_system_request(irp)`.
- If access is denied, completes the IRP immediately with `STATUS_ACCESS_DENIED`.

### 4.4 Storage Volume Redirection and DACL Hardening (`userspace/libusbip/`)
For storage class devices:
- Removes the volume's global mount point (`\GLOBAL??\X:`) using `DefineDosDeviceW`.
- Creates a private session drive letter in `\Sessions\<Id>\DosDevices\<LogonId>\X:`.
- Hardens volume object and directory DACL with `D:P(A;;GA;;;SY)(A;;GA;;;<LogonSID>)(A;;GA;;;<UserSID>)`.

---

## 5. Changes Across the Stack

| Component | File | Description |
|---|---|---|
| Core Types | `include/usbip/vhci.h` | Added `sid_data`, `isolation::user`, `session::invalid`, `owner_sid` in `get_port_isolation` |
| Security Library | `drivers/libdrv/security.h`, `security.cpp` | Implemented `get_requestor_sid`, `is_caller_sid`, `is_system_request`, `is_admin_request`, `equal_sid` |
| UDE Isolation & Context | `drivers/ude/isolation.h`, `isolation.cpp`, `context.h`, `context.cpp` | Stored `owner_sid` in `device_ctx` and `fileobject_ctx`; implemented `is_user_isolated()` |
| UDE IOCTL & Events | `drivers/ude/vhci_ioctl.cpp`, `vhci.cpp` | Captured caller SID in `plugin_hardware`; gated `plugout_hardware`, `get_imported_devices`, and events |
| Upper Filter Core | `drivers/ude_filter/device.h`, `device.cpp` | Stored `parent` pointer and isolation state in device extension |
| Upper Filter PnP | `drivers/ude_filter/pnp.cpp` | Queries port isolation and stamps `DEVPKEY_Device_SessionId` upon child device `IRP_MN_START_DEVICE` (Driver Verifier compliant) |
| Upper Filter Gate | `drivers/ude_filter/driver.cpp` | In `create`, enforced `is_caller_sid` / `is_system_request` for `isolation::user` |
| Volume Isolation | `userspace/libusbip/src/volume_isolation.cpp` | Hardened DACL with User SID and Logon SID; redirected drive letter to caller session |
| CLI Attach | `userspace/usbip/usbip.cpp`, `attach.cpp` | Added `-i, --isolate <none\|session\|user>`; invoked volume isolation for session and user modes |
| GUI Integration | `userspace/wusbip/wusbip.cpp` | Handled volume isolation when `iso_mode != isolation::none` |
| SDK Verification | `userspace/libusbip_check/main.cpp` | Verified C++17 compatibility with `isolation::user` |

---

## 6. Threat Matrix & Residuals Closed

| Security Concern | Previous Behavior | Hardened Multi-Layer Behavior |
|---|---|---|
| **PnP Devnode Visibility** | Device appeared in Device Manager for all sessions | `DEVPKEY_Device_SessionId` stamped on child PDO; hidden from Device Manager in foreign sessions |
| **Direct Device Opening** | Any process could open `\\.\USB#VID_...` or device interfaces | FiDO `IRP_MJ_CREATE` (`create`) rejects foreign interactive sessions and non-owner SIDs with `STATUS_ACCESS_DENIED` |
| **Storage Drive Letters** | MountMgr assigned drive letter in `\GLOBAL??`; visible to all sessions | Drive letter removed from `\GLOBAL??` and redirected to `\Sessions\<Id>\DosDevices\<LogonId>\` |
| **Storage Data Access** | All sessions (including admin) could read/write storage volume | Volume DACL restricted to `SYSTEM`, caller Logon SID, and User SID; foreign users and foreign admins receive `ERROR_ACCESS_DENIED` |
| **Control Plane Privacy** | `GET_IMPORTED_DEVICES` listed all devices machine-wide | Filtered in kernel; standard users only see devices owned by their session / SID |
| **Cross-Session/User Detach** | Any caller could detach any device or call mass detach | Gated in kernel; standard users can only detach devices matching their session / SID |
| **Attach Retries / Cancel** | Any session could cancel pending attach attempts | Gated in kernel; cancellation scoped to caller session and SID |
| **Registry Persistence** | Standard users could overwrite machine-wide auto-reattach | Gated in kernel; restricted to administrators |
