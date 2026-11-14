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
|    - Administrators retain management & recovery override for detach/list         |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 2. PnP Devnode Plane (usbip2_filter.sys)                                          |
|    - Upper filter on USB\ROOT_HUB30 queries port isolation via internal IOCTL     |
|    - Passes fltr.pdo in INTERNAL_GET_PORT_ISOLATION so usbip2_ude.sys records     |
|      DEVPKEY_Device_InstanceId and DEVPKEY_Device_ContainerId of the USB PDO      |
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
| 4. Storage & Volume Data Plane (usbip2_ude.sys, libdrv, libusbip, usbip, wusbip)  |
|    - Kernel PnP Hook: IoRegisterPlugPlayNotification (GUID_DEVINTERFACE_VOLUME)   |
|      immediately hardens \Device\HarddiskVolumeN DACL on volume arrival           |
|    - Kernel IOCTL: ISOLATE_PORT_VOLUME verifies volume-to-port disk extents,      |
|      deletes \GLOBAL??\X: in kernel mode, and hardens volume & root DACLs         |
|    - Drive Letter Redirection: libusbip maps the drive letter inside the caller's |
|      session namespace (\Sessions\<Id>\DosDevices\<LogonId>\X:) without elevation |
|    - Volume DACL Hardening: Protects volume device object & filesystem root with  |
|      SDDL D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;<LogonSID>)(A;OICI;GA;;;<UserSID>)      |
|    - Explicit Default-Deny: Administrators (BA) omitted from data plane DACL      |
|    - Non-Storage Fast-Path: Inspects devnode PnP class/service to skip timeouts   |
|    - Filesystem Awareness: Skips unsupported root ACLs on FAT32/exFAT             |
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
| **Upper Filter Gate (`IRP_MJ_CREATE`)** | Matches `IoGetRequestorSessionId(irp) == dev.session_id` | Matches `libdrv::is_caller_sid(irp, dev.owner.sid)` |
| **Kernel & SYSTEM Whitelist** | KernelMode, Session 0, `MAXULONG` | KernelMode, `SeLocalSystemSid` (`libdrv::is_system_request`) |
| **Foreign Administrator Access** | Management: allowed; Data plane: denied | Management: allowed; Data plane: denied |

---

## 3. Administrative Role Separation: Management vs. Data Plane

To satisfy the strict security requirement where administrators in foreign sessions or foreign accounts must not read or write user data:
- **Control Plane (Management)**: Local administrators can list ports and detach stuck or orphaned devices across sessions/users to prevent denial-of-service and roothub port exhaustion after unexpected user logoffs or machine disconnects.
- **Data Plane (Read/Write)**: Foreign administrators are strictly denied access to the device handles (`IRP_MJ_CREATE` rejects non-owning callers with `STATUS_ACCESS_DENIED`) and storage data (the volume device DACL omits `Administrators`, enforcing default-deny under the Windows Security Reference Monitor).

### 3.1 Privilege & Elevation Boundaries (Standard User vs. Administrator)

Windows Storage volume objects (`\Device\HarddiskVolumeN`) and global mount points (`\GLOBAL??\X:`) are owned by `SYSTEM` and `Administrators`, so unlinking `\GLOBAL??\X:` or modifying volume DACLs directly from user mode normally requires administrative elevation (`ERROR_ACCESS_DENIED` for standard users).

To allow **standard, non-elevated users** to achieve full device and storage volume isolation safely without administrative elevation (and without introducing a separate privileged Windows service), **usbip-win2** performs privileged storage hardening in kernel mode inside `usbip2_ude.sys`:

1. **Owner Identity Capture at Attach (`plugin_hardware`)**:
   - Captures the caller's User SID (`TokenUser`) and Logon SID (`TokenGroups` `SE_GROUP_LOGON_ID`) into `device_attributes`, regardless of isolation mode details.
2. **USB PDO Property Binding (`INTERNAL_GET_PORT_ISOLATION`)**:
   - When `usbip2_filter.sys` queries port isolation on child start, it passes `fltr.pdo`. `usbip2_ude.sys` records the child USB PDO's `DEVPKEY_Device_InstanceId` and `DEVPKEY_Device_ContainerId`.
3. **Automatic PnP Volume Arrival Hardening (`GUID_DEVINTERFACE_VOLUME`)**:
   - `usbip2_ude.sys` registers a kernel PnP interface notification for `GUID_DEVINTERFACE_VOLUME`. When a volume arrives, a work item verifies whether its disk extents (`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`) belong to any isolated USB port (via `GUID_DEVINTERFACE_DISK` PDO `DEVPKEY_Device_Parent` or `DEVPKEY_Device_ContainerId`) and immediately applies the owner-restricted DACL to `\Device\HarddiskVolumeN`. This closes the volume arrival race window and secures volumes even if userspace skips `isolate_port_volumes`.
4. **Owner-Gated Kernel Volume Isolation IOCTL (`ISOLATE_PORT_VOLUME`)**:
   - Accepts a drive letter and volume path from userspace, verifies port ownership (`is_requestor_owner`), verifies that the volume physically resides on the specified port's USB PDO, verifies that `\GLOBAL??\X:` resolves to `\Device\HarddiskVolumeN`, removes `\GLOBAL??\X:` via `IoDeleteSymbolicLink`, and hardens DACLs on both `\Device\HarddiskVolumeN` and `\Device\HarddiskVolumeN\` via `ZwSetSecurityObject`.
5. **Session-Local Drive Mapping in Userspace (`libusbip`)**:
   - `libusbip` calls `ISOLATE_PORT_VOLUME` for privileged global unmapping and DACL hardening, then creates the session-local `X:` symbolic link inside the caller's own interactive session namespace (`\Sessions\<Id>\DosDevices\<LogonId>\`) via `DefineDosDeviceW(DDD_RAW_TARGET_PATH, ...)`, which standard users are permitted to modify (falling back to user-mode operations if needed).

**Conclusion**: Standard, non-elevated users achieve complete USB device and storage volume isolation (including `\GLOBAL??` unmapping and volume/root DACL restriction) without administrative elevation.

---

## 4. Key Implementation Details

### 4.1 Zero-Allocation Kernel SID Representation (`sid_blob`)
To eliminate pool allocations, fragmentation, and potential leaks in kernel I/O paths, `sid_blob` embeds a fixed buffer matching `SECURITY_MAX_SID_SIZE` (68 bytes):
```cpp
enum { max_sid_size = 68 }; // SECURITY_MAX_SID_SIZE

struct sid_blob
{
        UCHAR data[max_sid_size];
        ULONG length;
};
```

### 4.2 Shared Kernel Security Library (`drivers/libdrv/security.h`)
Encapsulates token extraction, SID comparison, and kernel object security helpers using pure NT kernel APIs (`PsReferenceImpersonationToken`, `PsReferencePrimaryToken`, `SeQueryInformationToken`, `RtlEqualSid`, `ZwSetSecurityObject`, `IoDeleteSymbolicLink`):
- `get_requestor_sid(irp, sid)`: Inspects thread impersonation token first, falling back to process primary token (`TokenUser`).
- `get_requestor_logon_sid(irp, sid)`: Extracts the caller's interactive logon SID (`SE_GROUP_LOGON_ID` from `TokenGroups`).
- `is_caller_sid(irp, expected_sid)`: Validates caller SID against expected SID, always passing `KernelMode`.
- `is_system_request(irp)`: Validates caller against `SeLocalSystemSid` (or `KernelMode`).
- `is_admin_request(irp)`: Validates caller against `SeAliasAdminsSid` (or `KernelMode`).
- `set_volume_dacl(handle, user_sid, logon_sid)`: Builds a protected security descriptor (`SE_DACL_PROTECTED`) granting `GENERIC_ALL` with `OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE` to `SYSTEM`, `user_sid`, and `logon_sid` (omitting `Administrators`) and applies it via `ZwSetSecurityObject`.
- `remove_global_drive_letter(letter, expected_target)`: Opens `\GLOBAL??\<X>:`, verifies its symlink target matches `expected_target` (`\Device\HarddiskVolumeN`), and removes the global symbolic link via `IoDeleteSymbolicLink`.
- `operator==(a, b)`: Performs length check and `RtlEqualSid`.

### 4.3 Upper Filter Dispatch Gate (`drivers/ude_filter/driver.cpp`)
Intercepts `IRP_MJ_CREATE` before requests reach underlying device stacks:
- **Session Mode**: Checks `req_session == dev.session_id || mode == KernelMode || req_session == 0 || req_session == MAXULONG`.
- **User Mode**: Checks `libdrv::is_caller_sid(irp, dev.owner.sid) || libdrv::is_system_request(irp)`.
- If access is denied, completes the IRP immediately with `STATUS_ACCESS_DENIED`.

### 4.4 Kernel-Assisted Storage Volume Redirection and DACL Hardening (`drivers/ude/`, `drivers/libdrv/`, `userspace/libusbip/`)
For storage class devices:
- **Automatic PnP Volume Arrival Hardening**: `usbip2_ude.sys` registers `IoRegisterPlugPlayNotification` for `GUID_DEVINTERFACE_VOLUME` (synchronized with an `EX_RUNDOWN_REF` and `WDFWORKITEM`). Whenever a new volume interface arrives, the driver verifies whether the volume's extents belong to any isolated port and immediately hardens the `\Device\HarddiskVolumeN` DACL, closing the window between PnP volume arrival and userspace `MountMgr` discovery.
- **Kernel Volume-to-Port Ownership Verification**: Both PnP arrival hardening and `ISOLATE_PORT_VOLUME` open the volume device, query `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`, and verify every disk extent's `DiskNumber` resolves to a `GUID_DEVINTERFACE_DISK` PDO whose `DEVPKEY_Device_Parent` matches the port's `DEVPKEY_Device_InstanceId` (direct USB mass storage) or whose `DEVPKEY_Device_ContainerId` matches the port's `DEVPKEY_Device_ContainerId` with a `USB\` parent prefix (composite USB storage).
- **Hybrid Kernel/Userspace Drive Letter Redirection**: `libusbip` resolves the drive letter and `\Device\HarddiskVolumeN` target via `wait_for_volume_drive_letter`, calls `vhci::ioctl::ISOLATE_PORT_VOLUME` (`isolate_volume_kernel`) so `usbip2_ude.sys` deletes `\GLOBAL??\X:` (after target verification) and hardens both `\Device\HarddiskVolumeN` and `\Device\HarddiskVolumeN\` DACLs, and then calls `DefineDosDeviceW(DDD_RAW_TARGET_PATH, ...)` in the caller's interactive session to map `X:` inside `\Sessions\<Id>\DosDevices\<LogonId>\X:` (retaining user-mode operations as fallback).
- **Inherited DACL Hardening**: Restricts the volume device object and root directory with `D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;<LogonSID>)(A;OICI;GA;;;<UserSID>)`. The `OICI` flags ensure newly created child files and directories on NTFS volumes inherit full access for the owner while denying foreign access.
- **Filesystem Compatibility**: Checks `FILE_PERSISTENT_ACLS` via `GetVolumeInformationW`. On filesystems lacking persistent security descriptors (FAT32, exFAT), filesystem-level DACL modification is safely skipped without returning spurious errors, while keeping the kernel device object (`\Device\HarddiskVolumeN`) secured against raw sector access, formatting, and dismounting.
- **Non-Storage Device Fast Bailout**: Inspects `DEVPKEY_Device_Class` and `DEVPKEY_Device_Service` on the roothub port's devnode. Conclusively non-storage devices (HID, audio, camera, bluetooth) bypass volume discovery immediately, avoiding the 4-second timeout delay.

---

## 5. Changes Across the Stack

| Component | File | Description |
|---|---|---|
| Core Types | `include/usbip/vhci.h`, `userspace/libusbip/vhci.h` | Added `sid_blob`, `device_owner`, `isolation::user`, `device_config::user`, `ISOLATE_PORT_VOLUME`, `get_port_isolation::pdo` |
| Security Library | `drivers/libdrv/security.h`, `security.cpp`, `dbgcommon.cpp` | Implemented `get_requestor_sid`, `get_requestor_logon_sid`, `is_caller_sid`, `is_system_request`, `is_admin_request`, `set_volume_dacl`, `remove_global_drive_letter`, `is_valid`, `operator==` |
| UDE Isolation & Context | `drivers/ude/isolation.h`, `isolation.cpp`, `context.h`, `context.cpp` | Stored `device_owner`, caller user/logon SIDs, and USB PDO `instance_id`/`container_id` in `device_attributes`; implemented `verify_volume_on_device`, `isolate_device_volume`, and `register_volume_notification` |
| UDE IOCTL & Events | `drivers/ude/vhci_ioctl.cpp`, `vhci.cpp` | Captured caller SIDs in `plugin_hardware`; recorded PDO properties in `get_port_isolation`; added `isolate_port_volume`; registered PnP volume arrival notification |
| Upper Filter Core | `drivers/ude_filter/device.h`, `device.cpp` | Stored `parent` pointer and isolation state in device extension; passed `fltr.pdo` in `query_port_isolation` |
| Upper Filter PnP | `drivers/ude_filter/pnp.cpp` | Queries port isolation (with `fltr.pdo`) and stamps `DEVPKEY_Device_SessionId` upon child device `IRP_MN_START_DEVICE` (Driver Verifier compliant) |
| Upper Filter Gate | `drivers/ude_filter/driver.cpp` | In `create`, enforced `is_caller_sid` / `is_system_request` for `isolation::user` |
| Volume Isolation | `userspace/libusbip/volume_isolation.h`, `src/volume_isolation.cpp` | Added `isolate_volume_kernel` (`ISOLATE_PORT_VOLUME`) for standard-user global unmapping and DACL hardening; redirected drive letters in session; retained user-mode fallback and FAT32/exFAT/non-storage checks |
| CLI Attach & Detach | `userspace/usbip/usbip.cpp`, `attach.cpp`, `detach.cpp` | Added `-i, --isolate <none\|session\|user>`; invoked volume isolation for session and user modes; cleaned up session drive letters on detach |
| GUI Integration | `userspace/wusbip/wusbip.cpp` | Handled volume isolation on attach; added session drive letter cleanup on detach |
| SDK Verification | `userspace/libusbip_check/main.cpp` | Verified C++17 compatibility with `isolation::user` |

---

## 6. Threat Matrix & Residuals Closed

| Security Concern | Previous Behavior | Hardened Multi-Layer Behavior |
|---|---|---|
| **PnP Devnode Visibility** | Device appeared in Device Manager for all sessions | `DEVPKEY_Device_SessionId` stamped on child PDO; hidden from Device Manager in foreign sessions |
| **Direct Device Opening** | Any process could open `\\.\USB#VID_...` or device interfaces | FiDO `IRP_MJ_CREATE` (`create`) rejects foreign interactive sessions and non-owner SIDs with `STATUS_ACCESS_DENIED` |
| **Storage Drive Letters** | MountMgr assigned drive letter in `\GLOBAL??`; visible to all sessions | Drive letter removed from `\GLOBAL??` and redirected to `\Sessions\<Id>\DosDevices\<LogonId>\` |
| **Storage Data Access** | All sessions (including admin) could read/write storage volume | Volume DACL restricted to `SYSTEM`, caller Logon SID, and User SID with `OICI` inheritance; foreign users and foreign admins receive `ERROR_ACCESS_DENIED` |
| **Control Plane Privacy** | `GET_IMPORTED_DEVICES` listed all devices machine-wide | Filtered in kernel; standard users only see devices owned by their session / SID |
| **Cross-Session/User Detach** | Any caller could detach any device or call mass detach | Gated in kernel; standard users can only detach devices matching their session / SID |
| **Attach Retries / Cancel** | Any session could cancel pending attach attempts | Gated in kernel; cancellation scoped to caller session and SID |
| **Registry Persistence** | Standard users could overwrite machine-wide auto-reattach | Gated in kernel; restricted to administrators |
