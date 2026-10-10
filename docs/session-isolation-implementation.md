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
|    - WSK caller impersonation (SeCreateClientSecurity / SeImpersonateClientEx)    |
|      evaluates DNS & TCP connect against the user token for WFP firewalling       |
|    - Per-user non-admin port quota (MaxPortsPerUser) prevents port exhaustion     |
|    - INTERNAL_GET_PORT_ISOLATION provides port isolation state to filters         |
|    - GET_IMPORTED_DEVICES, PLUGOUT_HARDWARE, and event stream filter by owner     |
|    - IoRegisterContainerNotification auto-detaches session devices on logoff      |
|    - Administrators retain management & recovery override for detach/list         |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 2. PnP Devnode Plane (usbip2_filter.sys)                                          |
|    - Upper filter on USB\ROOT_HUB30 queries port isolation via internal IOCTL     |
|    - Passes fltr.pdo in INTERNAL_GET_PORT_ISOLATION so usbip2_ude.sys records     |
|      DEVPKEY_Device_InstanceId and DEVPKEY_Device_ContainerId of the USB PDO      |
|    - Attaches child FiDOs to usbccgp.sys (MI_xx) and HidUsb (HID\VID_...) PDOs    |
|    - Synchronously stamps DEVPKEY_Device_SessionId on USB, MI_xx, and HID PDOs    |
|    - Applies stack-wide kernel DACL (set_device_dacl via IopSetDeviceSecurity-    |
|      Descriptors) across PDO -> FiDO -> FDO (including COM/audio/video FDOs)     |
|    - Hides devnodes from Device Manager and PnP enumerations in foreign sessions  |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 3. Device IRP Security Plane (usbip2_filter.sys)                                  |
|    - Upper filter FiDO enforces FILE_DEVICE_SECURE_OPEN across entire namespace   |
|    - IRP_MJ_CREATE (create) checks SessionId and User SID against owner           |
|    - Rejects foreign users/sessions (standard & admin) with STATUS_ACCESS_DENIED  |
|    - Permits Session 0 / SYSTEM, LOCAL SERVICE (WUDFHost/audio/smartcard),        |
|      NETWORK SERVICE, and KernelMode callers                                      |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
| 4. Storage & Volume Data Plane (usbip2_ude.sys, libdrv, libusbip, usbip, wusbip)  |
|    - Kernel PnP Hook: IoRegisterPlugPlayNotification (GUID_DEVINTERFACE_VOLUME)   |
|      hardens \Device\HarddiskVolumeN & disk stack DACLs and unlinks \GLOBAL??\X:  |
|    - Kernel IOCTL: ISOLATE_PORT_VOLUME verifies volume-to-port disk extents,      |
|      deletes or returns removed \GLOBAL??\X:, and hardens volume & disk DACLs     |
|    - Drive Letter Redirection: libusbip maps the drive letter inside the caller's |
|      session namespace (\Sessions\<Id>\DosDevices\<LogonId>\X:) without elevation |
|    - Kernel Object DACL Hardening: Protects volume & disk device objects with     |
|      SDDL D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;<LogonSID>)(A;OICI;GA;;;<UserSID>)      |
|    - Explicit Default-Deny: Administrators (BA) omitted from data plane DACL      |
|    - Non-Storage Fast-Path: Inspects devnode PnP class/service to skip timeouts   |
|    - Kernel-Only DACLs: Never writes persistent SID ACLs onto on-disk NTFS media  |
+-----------------------------------------------------------------------------------+
```

---

## 2. Comparison: Session Isolation vs. User Isolation

| Feature / Dimension | Session Isolation (`-i session`) | User-Based Isolation (`-i user`) |
|---|---|---|
| **Identity Anchor** | `SessionId` (Terminal Server) + attaching User SID (`caller_user_sid`) | Immutable User SID (`TokenUser` via `SeQueryInformationToken`) |
| **Session Disconnect / Reconnect** | If RDS reassigns a new session ID, device ownership is bound to the old ID | Retains ownership regardless of session ID reassignment |
| **Session Logoff / Termination** | `IoRegisterContainerNotification` (`IO_SESSION_STATE_LOGOFF_EVENT \| IO_SESSION_STATE_TERMINATION_EVENT`) stops attach attempts and detaches `isolation::session` devices for that `SessionId` | Remains attached and bound to User SID across logoff/reconnect |
| **Fast User Switching** | Different session ID per logged-in user | Bound to user SID; invisible/inaccessible across accounts |
| **Console `runas` / Service Impersonation** | Enforces `caller_user_sid` alongside `SessionId` (blocks cross-user `runas` in same session) | Distinguishes callers by effective user token / impersonation token |
| **PnP Devnode Stamping** | Stamps `DEVPKEY_Device_SessionId` on USB PDO, `usbccgp` `MI_xx`, and `HidUsb` `HID\...` child/grandchild PDOs | Does not stamp `DEVPKEY_Device_SessionId` (preserves access across session ID changes) |
| **Stack-Wide Kernel DACLs** | Applies `set_device_dacl` (`SYSTEM`, `LOCAL SERVICE`, `caller_user_sid`; no `Everyone`/`Administrators`) across `PDO -> FiDO -> FDO` | Applies `set_device_dacl` (`SYSTEM`, `LOCAL SERVICE`, `owner.sid`; no `Everyone`/`Administrators`) across `PDO -> FiDO -> FDO` |
| **Upper Filter Gate (`IRP_MJ_CREATE`)** | Matches `SessionId` and `caller_user_sid` | Matches `libdrv::is_caller_sid(irp, dev.owner.sid)` or `libdrv::is_service_sid(sid)` |
| **Kernel & Service Whitelist** | `KernelMode`, Session 0 (`SYSTEM`, `LOCAL SERVICE`, `NETWORK SERVICE`), `MAXULONG` | `KernelMode`, `is_service_sid` (`SYSTEM`, `LOCAL SERVICE`, `NETWORK SERVICE`) |
| **Foreign Administrator Access** | Management: allowed; Data plane: denied | Management: allowed; Data plane: denied |

---

## 3. Administrative Role Separation: Management vs. Data Plane

To satisfy the strict security requirement where administrators in foreign sessions or foreign accounts must not read or write user data:
- **Control Plane (Management)**: Local administrators can list ports and detach stuck or orphaned devices across sessions/users to prevent denial-of-service and roothub port exhaustion after unexpected user logoffs or machine disconnects.
- **Data Plane (Read/Write)**: Foreign administrators are strictly denied access to the device handles (`IRP_MJ_CREATE` rejects non-owning callers with `STATUS_ACCESS_DENIED`) and storage data (the volume and disk device DACLs omit `Administrators`, enforcing default-deny under the Windows Security Reference Monitor).

### 3.1 Privilege & Elevation Boundaries (Standard User vs. Administrator)

Windows Storage volume objects (`\Device\HarddiskVolumeN`), disk objects (`\Device\HarddiskN\DR0`), and global mount points (`\GLOBAL??\X:`) are owned by `SYSTEM` and `Administrators`, so unlinking `\GLOBAL??\X:` or modifying volume/disk DACLs directly from user mode normally requires administrative elevation (`ERROR_ACCESS_DENIED` for standard users).

To allow **standard, non-elevated users** to achieve full device and storage volume isolation safely without administrative elevation (and without introducing a separate privileged Windows service), **usbip-win2** performs privileged storage hardening in kernel mode inside `usbip2_ude.sys`:

1. **Owner Identity Capture at Attach (`plugin_hardware`)**:
   - Captures the caller's User SID (`TokenUser`) and Logon SID (`TokenGroups` `SE_GROUP_LOGON_ID`) into `device_attributes` (and preserves them across retries via `plugin_hardware_ex`).
2. **USB PDO Property Binding (`INTERNAL_GET_PORT_ISOLATION`)**:
   - When `usbip2_filter.sys` queries port isolation on child start, it passes `fltr.pdo`. `usbip2_ude.sys` records the child USB PDO's `DEVPKEY_Device_InstanceId` and `DEVPKEY_Device_ContainerId`, and returns `caller_user_sid` for session-mode filter checks.
3. **Automatic PnP Volume Arrival Hardening (`GUID_DEVINTERFACE_VOLUME`)**:
   - `usbip2_ude.sys` registers a kernel PnP interface notification for `GUID_DEVINTERFACE_VOLUME`. When a volume arrives, `on_volume_arrival` verifies whether its disk extents (`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`) belong to any isolated USB port (via `GUID_DEVINTERFACE_DISK` PDO `DEVPKEY_Device_Parent` or `DEVPKEY_Device_ContainerId`), immediately applies the owner-restricted DACL to both `\Device\HarddiskVolumeN` and the underlying `GUID_DEVINTERFACE_DISK` device stack (`\Device\HarddiskN\DR0`), and deletes `\GLOBAL??\X:` as a kernel fallback (recording the letter for `ISOLATE_PORT_VOLUME`). This closes the volume arrival race window and removes global drive letters even if userspace omits or crashes before `isolate_port_volumes`.
4. **Owner-Gated Kernel Volume Isolation IOCTL (`ISOLATE_PORT_VOLUME`)**:
   - Accepts a drive letter (optional) and volume path from userspace, verifies port ownership, verifies that the volume physically resides on the specified port's USB PDO, hardens both `\Device\HarddiskVolumeN` and the underlying `GUID_DEVINTERFACE_DISK` stack DACLs via `ZwSetSecurityObject`, and removes `\GLOBAL??\X:` via `ZwMakeTemporaryObject` (or returns the letter already removed by `on_volume_arrival`).
5. **Session-Local Drive Mapping in Userspace (`libusbip`)**:
   - `libusbip` calls `ISOLATE_PORT_VOLUME` for privileged global unmapping and volume/disk DACL hardening, then creates the session-local `X:` symbolic link inside the caller's own interactive session namespace (`\Sessions\<Id>\DosDevices\<LogonId>\`) via `DefineDosDeviceW(DDD_RAW_TARGET_PATH, ...)`, which standard users are permitted to modify.

**Conclusion**: Standard, non-elevated users achieve complete USB device and storage volume isolation (including `\GLOBAL??` unmapping and volume/disk DACL restriction) without administrative elevation or persistent on-disk ACL side effects.

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
Encapsulates token extraction, SID comparison, and kernel object security helpers using pure NT kernel APIs (`PsReferenceImpersonationToken`, `PsReferencePrimaryToken`, `SeQueryInformationToken`, `RtlEqualSid`, `ZwSetSecurityObject`, `ZwMakeTemporaryObject`):
- `get_requestor_sid(irp, sid)`: Inspects thread impersonation token first, falling back to process primary token (`TokenUser`).
- `get_requestor_logon_sid(irp, sid)`: Extracts the caller's interactive logon SID (`SE_GROUP_LOGON_ID` from `TokenGroups`).
- `is_caller_sid(irp, expected_sid)`: Validates caller SID against expected SID, always passing `KernelMode`.
- `is_system_request(irp)`: Validates caller against `SeLocalSystemSid` (or `KernelMode`).
- `is_service_sid(sid)`: Validates caller against built-in `NT AUTHORITY` service SIDs (`SeLocalSystemSid`, `SeLocalServiceSid`, `SeNetworkServiceSid`) so UMDF (`WUDFHost.exe`), `Audiosrv`, `FrameServer`, and `SCardSvr` can open device stacks in `isolation::user` mode.
- `is_admin_request(irp)`: Validates caller against `SeAliasAdminsSid` (or `KernelMode`).
- `set_volume_dacl(handle, user_sid, logon_sid)`: Builds a protected security descriptor (`SE_DACL_PROTECTED`) granting `GENERIC_ALL` with `OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE` to `SYSTEM`, `user_sid`, and `logon_sid` (omitting `Administrators`) and applies it via `ZwSetSecurityObject`.
- `set_device_dacl(devobj, user_sid)`: Opens a direct kernel handle to `devobj` via `ObOpenObjectByPointer(..., nullptr, KernelMode, &h)` and invokes `set_volume_dacl(h, user_sid, local_service)`. Because the handle has `FileObject == NULL`, `ZwSetSecurityObject` triggers `ntoskrnl!IopSetDeviceSecurityDescriptors`, walking `AttachedDevice` from `PDO -> FiDO -> FDO` to lock down the entire device stack (including class FDOs such as `usbser.sys`, `hidclass.sys`, `usbaudio.sys`, `usbvideo.sys`, and `WinUSB.sys`) while permitting `SYSTEM` and `LOCAL SERVICE` (`WUDFHost`, audio/smartcard services).
- `remove_global_drive_letter(letter, expected_target)` / `remove_global_drive_letter(expected_target, letter)`: Opens `\GLOBAL??\<X>:` (or scans `A:`..`Z:`), verifies its symlink target matches `expected_target` (`\Device\HarddiskVolumeN`), and marks the global symbolic link temporary via `ZwMakeTemporaryObject`.
- `operator==(a, b)`: Performs length check and `RtlEqualSid`.

### 4.3 Upper Filter Dispatch Gate & Composite / HID Filtering (`drivers/ude_filter/`)
- **Composite & HID Child/Grandchild PDO Attachment (`pnp.cpp`)**: Intercepts `IRP_MN_QUERY_DEVICE_RELATIONS` (`BusRelations`) across all filter tiers (`is_hub()`, `is_device()`, and `is_function()`), attaching child `is_function()` FiDOs to both `\Driver\usbccgp` (`USB\VID_...&MI_xx`) and `\Driver\HidUsb` (`HID\VID_...`, i.e., `hidclass.sys` child/grandchild PDOs) via `is_filterable_child_driver()`. `USBD_CreateHandle`, `IRP_MN_QUERY_INTERFACE`, and URB post-processing remain restricted to top-level `is_device()` FiDOs so URBs are never intercepted twice.
- **`DEVPKEY_Device_SessionId` Stamping (`pnp.cpp`)**: Stamps `DEVPKEY_Device_SessionId` in `IRP_MN_START_DEVICE` on top-level USB PDOs, `usbccgp` child `MI_xx` PDOs, and `HidUsb` child/grandchild `HID\...` PDOs for `isolation::session`.
- **Stack-Wide Kernel DACL Propagation (`pnp.cpp`)**: In `apply_isolation` (`IRP_MN_START_DEVICE`), calls `libdrv::set_device_dacl(fltr.pdo, dev.iso.owner.sid)` whenever `owner.has_sid()` is valid (`isolation::user` and `isolation::session`), hardening every `DEVICE_OBJECT` from the PDO up through the top FDO (`PDO -> FiDO -> FDO`) before user-mode applications open class interfaces or FDO symbolic links (such as `\GLOBAL??\COMx -> \Device\USBSER000`).
- **`IRP_MJ_CREATE` Dispatch Gate (`driver.cpp`)**: Intercepts `IRP_MJ_CREATE` before requests reach underlying device stacks:
  - **Session Mode**: Checks `req_session == dev.session_id && (!owner.has_sid() || is_user_access_allowed)` (permitting `KernelMode`, Session 0 / `MAXULONG`, and `is_service_sid` service accounts).
  - **User Mode**: Checks `is_service_sid(sid) || (owner.has_sid() && owner.sid == sid)`.
  - If access is denied, completes the IRP immediately with `STATUS_ACCESS_DENIED`.

### 4.4 Kernel-Assisted Storage Volume Redirection and DACL Hardening (`drivers/ude/`, `drivers/libdrv/`, `userspace/libusbip/`)
For storage class devices:
- **Automatic PnP Volume Arrival Hardening & Drive Letter Fallback**: `usbip2_ude.sys` registers `IoRegisterPlugPlayNotification` for `GUID_DEVINTERFACE_VOLUME` (synchronized with an `EX_RUNDOWN_REF` and `WDFWORKITEM`). Whenever a new volume interface arrives, `on_volume_arrival` verifies whether the volume's extents belong to any isolated port, hardens both the `\Device\HarddiskVolumeN` DACL and the underlying `GUID_DEVINTERFACE_DISK` (`\Device\HarddiskN\DR0`) device stack DACL, and polls briefly to delete `\GLOBAL??\X:` if userspace has not already done so (recording the removed drive letter in `device_attributes::volume_letters`).
- **Kernel Volume-to-Port Ownership Verification**: Both PnP arrival hardening and `ISOLATE_PORT_VOLUME` open the volume device, query `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`, and verify every disk extent's `DiskNumber` resolves to a `GUID_DEVINTERFACE_DISK` PDO whose `DEVPKEY_Device_Parent` matches the port's `DEVPKEY_Device_InstanceId` (direct USB mass storage) or whose `DEVPKEY_Device_ContainerId` matches the port's `DEVPKEY_Device_ContainerId` with a `USB\` parent prefix (composite USB storage). On match, `set_volume_dacl` is applied to the disk device handle so `IopSetDeviceSecurityDescriptors` hardens the entire disk stack against foreign Administrators opening `\\.\PhysicalDriveN`.
- **Hybrid Kernel/Userspace Drive Letter Redirection**: `libusbip` resolves `\Device\HarddiskVolumeN`, calls `vhci::ioctl::ISOLATE_PORT_VOLUME` (`isolate_volume_kernel`) so `usbip2_ude.sys` hardens the volume and disk DACLs and deletes (or returns) `\GLOBAL??\X:`, and then calls `DefineDosDeviceW(DDD_RAW_TARGET_PATH, ...)` in the caller's interactive session to map `X:` inside `\Sessions\<Id>\DosDevices\<LogonId>\X:`.
- **Kernel-Only In-Memory DACLs (No On-Disk NTFS Mutation)**: Hardening `\Device\HarddiskVolumeN` (without trailing backslash) sets the in-memory device object DACL checked by `ntoskrnl!IopParseDevice` (`FILE_DEVICE_DISK` bypasses `TOKEN_HAS_TRAVERSE_PRIVILEGE` and enforces `FILE_TRAVERSE` on the device object DACL for all file/directory opens), blocking foreign users and foreign administrators without writing persistent SID-locked ACLs into `$Secure` on portable NTFS drives.
- **Non-Storage Device Fast Bailout**: Inspects `USB_DEVICE_DESCRIPTOR` / configuration descriptors (`imported_device_hardware::is_non_storage()`) in the driver so conclusively non-storage devices bypass volume discovery immediately without waiting for timeouts.

### 4.5 Automatic Session Logoff & Termination Cleanup (`drivers/ude/`)
To prevent Session ID recycling from exposing orphaned `isolation::session` devices to a subsequent user assigned the same `SessionId`:
- `usbip2_ude.sys` registers `IoRegisterContainerNotification` (`IoSessionStateNotification`) for `IO_SESSION_STATE_LOGOFF_EVENT | IO_SESSION_STATE_TERMINATION_EVENT` in `create_vhci` (`register_session_notification`) and unregisters it in `vhci_cleanup` (`unregister_session_notification`).
- When a session logs off or terminates, `session_notification_callback` queries `IoGetContainerInformation(IoSessionStateInformation, ...)` for `SessionId`, cancels pending attach retries via `stop_attach_attempts(vhci, 0, &owner)`, and queues an owner-scoped `async_detach_and_delete_all(vhci_dev, &owner)` using kernel-internal `plugout_hardware_ex` on `ctx.target_self`.
- `complete()` schedules any retry (`start_attach_attempts`) before calling `WdfRequestComplete()` on the sequential default queue, and `detach_all()` also calls `stop_attach_attempts(*get_vhci_ctx(vhci), 0, owner)` when `owner != nullptr`, serializing retry cancellation after any in-flight `PLUGIN_HARDWARE` and closing the logoff attach-retry race.
- `plugout_hardware` recognizes `KernelMode` requests with `plugout_hardware_ex` and scopes `detach_all` to the specified `device_owner`, detaching all `isolation::session` devices belonging to that `SessionId` on logoff (`!owner.has_sid()`) while enforcing `caller_user_sid` matching for user-mode callers and leaving `isolation::user` and `isolation::none` devices untouched.

### 4.6 WSK Caller Impersonation & Per-User Port Quotas (`drivers/ude/`)
To eliminate outbound SYSTEM-context SSRF / firewall bypass and prevent non-admin port exhaustion on the shared VHCI:
- **Caller Security Context Impersonation (`vhci_ioctl.cpp`)**: In `plugin_hardware`, when `WdfRequestGetRequestorMode(request) != KernelMode`, `usbip2_ude.sys` captures a static `SECURITY_CLIENT_CONTEXT` from `irp->Tail.Overlay.Thread` via `SeCreateClientSecurity` (`SecurityImpersonation`, `EffectiveOnly = true`) and transfers ownership across `workitem_ctx` hops (freeing it via `SeDeleteClientSecurity` in `workitem_cleanup`). Around `wsk::getaddrinfo()` and `create_socket()`/`bind()`/`connect()`, a scoped `impersonation_guard` calls `SeImpersonateClientEx` and reverts via `PsRevertToSelf()`, so Windows Filtering Platform (WFP) and firewall policy evaluate outbound DNS/TCP traffic against the attaching user's token rather than `NT AUTHORITY\SYSTEM`.
- **Per-User Port Quotas (`context.h`, `vhci.cpp`, `isolation.cpp`, `vhci_ioctl.cpp`)**: `vhci_ctx::max_user_ports` defaults to `max(1, devices_cnt / 2)` (configurable via `MaxPortsPerUser` under `DriverRegKeyParameters`, clamped to `1..devices_cnt`). `init_requestor_isolation` records `caller_user_sid` across all user-mode attaches (including `-i none`), and `plugin_hardware` rejects non-admin requests with `STATUS_QUOTA_EXCEEDED` once `vhci::count_user_devices(vhci, caller_user_sid) >= max_user_ports`.

---

## 5. Changes Across the Stack

| Component | File | Description |
|---|---|---|
| Core Types | `include/usbip/vhci.h`, `userspace/libusbip/vhci.h` | Added `sid_blob`, `device_owner`, `isolation::user`, `device_config::user`, `ISOLATE_PORT_VOLUME` (`output_size()`), `get_port_isolation::pdo` |
| Security Library | `drivers/libdrv/security.h`, `security.cpp`, `dbgcommon.cpp` | Implemented `get_requestor_sid`, `get_requestor_logon_sid`, `is_caller_sid`, `is_system_request`, `is_service_sid`, `is_admin_request`, `set_volume_dacl`, `set_device_dacl` (stack-wide `PDO->FDO` DACL propagation), `remove_global_drive_letter`, `is_valid`, `operator==` |
| UDE Isolation & Context | `drivers/ude/isolation.h`, `isolation.cpp`, `context.h`, `context.cpp` | Stored `device_owner`, caller user/logon SIDs (including `caller_user_sid` on `-i none` for quota accounting), `max_user_ports`, `volume_letters`, and USB PDO `instance_id`/`container_id`; enforced `caller_user_sid` in `isolation::session`; hardened volume & `GUID_DEVINTERFACE_DISK` DACLs; added `\GLOBAL??\X:` fallback in `on_volume_arrival`; removed `harden_volume_root` |
| UDE Device & IOCTL | `drivers/ude/ioctl.h`, `device.h`, `device.cpp`, `persistent.cpp`, `vhci_ioctl.cpp`, `vhci.h`, `vhci.cpp` | Added WSK caller impersonation (`SeCreateClientSecurity`/`SeImpersonateClientEx`); enforced `MaxPortsPerUser` (`count_user_devices`); preserved caller SIDs across reattach (`plugin_hardware_ex`); closed logoff retry race in `complete`/`detach_all`; returned kernel-removed drive letter in `isolate_port_volume`; returned `caller_user_sid` for session mode in `get_port_isolation` |
| Upper Filter Core | `drivers/ude_filter/device.h`, `device.cpp` | Replaced `is_hub` bool with 3-tier hierarchy (`is_hub()`, `is_device()`, `is_function()`), `is_filterable_child_driver()` (`\Driver\usbccgp` + `\Driver\HidUsb`), shared `previous` tracking |
| Upper Filter PnP & URB | `drivers/ude_filter/pnp.cpp`, `int_dev_ctrl.cpp` | Attached child/grandchild FiDOs to `usbccgp` `MI_xx` and `HidUsb` `HID\...` PDOs; stamped `DEVPKEY_Device_SessionId`; applied stack-wide `set_device_dacl`; skipped URB/query-interface interception on child function FiDOs |
| Upper Filter Gate | `drivers/ude_filter/driver.cpp` | In `create`, allowed `is_service_sid` + owner SID for `isolation::user` and `caller_user_sid` alongside `session_id` for `isolation::session` |
| Volume Isolation | `userspace/libusbip/volume_isolation.h`, `src/volume_isolation.cpp` | Added `isolate_volume_kernel` (`ISOLATE_PORT_VOLUME`) accepting kernel-removed drive letters; redirected drive letters in session; added non-storage fast bailout |
| CLI Attach & Detach | `userspace/usbip/usbip.cpp`, `attach.cpp`, `detach.cpp` | Added `-i, --isolate <none\|session\|user>`; invoked volume isolation for session and user modes; cleaned up session drive letters on detach |
| GUI Integration | `userspace/wusbip/wusbip.cpp` | Handled volume isolation on attach; added session drive letter cleanup on detach |
| SDK Verification | `userspace/libusbip_check/main.cpp` | Verified C++17 compatibility with `isolation::user` |

---

## 6. Threat Matrix & Residuals Closed

| Security Concern | Previous Behavior | Hardened Multi-Layer Behavior |
|---|---|---|
| **PnP Devnode Visibility** | Device appeared in Device Manager for all sessions | `DEVPKEY_Device_SessionId` stamped on child USB PDO, `usbccgp` `MI_xx`, and `HidUsb` `HID\...` PDOs; hidden from Device Manager in foreign sessions |
| **Direct Device & Class FDO Opening** | Any process could open `\\.\USB#VID_...`, `HID\...`, or named class FDO symlinks (`COMx`, audio/video) | Stack-wide `set_device_dacl` (`PDO -> FiDO -> FDO`, omitting `Everyone` & `Administrators`) plus FiDO `IRP_MJ_CREATE` reject foreign users, cross-user `runas`, and foreign admins with `STATUS_ACCESS_DENIED` |
| **Outbound WSK Attribution & Port Quota** | `PLUGIN_HARDWARE` connected as `SYSTEM` with unlimited non-admin port claims | `SeCreateClientSecurity` / `SeImpersonateClientEx` attributes DNS/TCP connect to caller token for WFP; `MaxPortsPerUser` prevents single non-admin user port starvation |
| **Storage Drive Letters** | MountMgr assigned drive letter in `\GLOBAL??`; visible to all sessions | Drive letter removed from `\GLOBAL??` (with kernel fallback in `on_volume_arrival`) and redirected to `\Sessions\<Id>\DosDevices\<LogonId>\` |
| **Storage Data & Raw Disk Access** | All sessions (including admin) could read/write storage volume or `\\.\PhysicalDriveN` | Both `\Device\HarddiskVolumeN` and `GUID_DEVINTERFACE_DISK` (`\Device\HarddiskN\DR0`) DACLs restricted to `SYSTEM`, caller Logon SID, and User SID; foreign users and foreign admins receive `ERROR_ACCESS_DENIED` without mutating on-disk NTFS ACLs |
| **Control Plane Privacy** | `GET_IMPORTED_DEVICES` listed all devices machine-wide | Filtered in kernel by session and User SID; standard users only see devices they own |
| **Cross-Session/User Detach** | Any caller could detach any device or call mass detach | Gated in kernel; standard users can only detach devices matching their session and User SID |
| **Attach Retries / Cancel** | Any session could cancel pending attach attempts | Gated in kernel; cancellation scoped to caller session and User SID; logoff retry race serialized |
| **Registry Persistence** | Standard users could overwrite machine-wide auto-reattach | Gated in kernel; restricted to administrators |
| **Session ID Recycling on Logoff** | Session-isolated devices stayed attached after logoff and could be inherited if Windows reused the `SessionId` | `IoRegisterContainerNotification` automatically stops attach attempts and detaches `isolation::session` devices on logoff/termination |

---

## 7. Architectural Boundaries & Residual Caveats

1. **Remaining Unfiltered Non-USB Child PDOs & `isolation::user` Device Manager Visibility**
   - `usbip2_filter.sys` filters `USB\ROOT_HUB30` child USB PDOs, `usbccgp.sys` (`USB\VID_...&MI_xx`) composite function PDOs, and `\Driver\HidUsb` (`hidclass.sys` `HID\VID_...`) HID collection PDOs, and applies stack-wide `set_device_dacl` across `PDO -> FiDO -> FDO` (protecting direct USB PDOs, `MI_xx` PDOs, `HID\...` stacks, and FDO device objects such as `usbser.sys`'s `\Device\USBSER000` backing `\GLOBAL??\COMx`, `usbaudio.sys`, `usbvideo.sys`, and `WinUSB.sys` in both `-i session` and `-i user` modes).
   - Remaining class buses that enumerate secondary grandchild PDOs outside `\Driver\usbccgp` and `\Driver\HidUsb` (for example BT/BTH or vendor sub-bus PDOs) rely on `DEVPKEY_Device_SessionId` in `-i session` mode; global symbolic links like `\GLOBAL??\COMx` remain visible by name across sessions even though opening `\Device\USBSER000` is blocked by the stack DACL for foreign users and foreign admins.
   - In `isolation::user` mode, `DEVPKEY_Device_SessionId` is intentionally omitted so ownership survives RDS session ID reassignments; consequently, devnodes remain visible in Device Manager (Windows PnP scopes UI/enumeration visibility only by `DEVPKEY_Device_SessionId`, not User SID), while data opens fail at the kernel DACL and FiDO `IRP_MJ_CREATE` boundaries.

2. **Raw Unpartitioned USB Disks (Zero Volumes)**
   - Disk DACL hardening (`GUID_DEVINTERFACE_DISK` / `\Device\HarddiskN\DR0`) is triggered during `GUID_DEVINTERFACE_VOLUME` arrival and `ISOLATE_PORT_VOLUME`. A completely unpartitioned USB disk with zero volumes will not trigger `GUID_DEVINTERFACE_VOLUME`.

3. **Session Drive Letter Cleanup on Unexpected Detach**
   - If a device disconnects unexpectedly (network drop, logoff teardown, or admin detach) without the owning session running `cleanup_port_volumes()`, the session-local `X:` symbolic link in `\Sessions\<Id>\DosDevices\<LogonId>\` remains until session logoff.

4. **Disconnected (Abandoned) RDS Sessions vs. Logoff**
   - `register_session_notification` listens for `IO_SESSION_STATE_LOGOFF_EVENT | IO_SESSION_STATE_TERMINATION_EVENT` and intentionally ignores `IO_SESSION_STATE_DISCONNECT_EVENT` so devices survive temporary RDP disconnects and Fast User Switching. Disconnected sessions that are never logged off retain their attached devices until an RDS session timeout or administrator detach occurs.

### 7.5 Per-User / Per-Session Virtual HCI Instances (Architectural Option #3)
An alternative control-plane architecture is spawning one virtual `usbip2_ude.sys` host controller (`ROOT\USBIP_WIN2\UDE`) per user or per session—either dynamically via a root bus enumerator / software-device (`SwDeviceCreate`) broker service or via multiple `devnode.exe` controller instances:
- **What Per-User / Per-Session VHCIs Solve:**
  - **Native Handle-Level SDDL Isolation**: Each virtual HCI `WDFDEVICE` can be created with an instance-specific SDDL (`D:P(A;;GA;;;SY)(A;;GA;;;<UserSID>)`), blocking cross-user `CreateFile` opens of foreign VHCI instances at the Object Manager layer without relying solely on per-IOCTL checks.
  - **Isolated Port Pools & Event Queues**: Each user gets an independent `UDECXUSBDEVICE[]` roothub array (`devices_cnt`), event subscriber list, and retry queue, structurally eliminating port-pool contention and cross-user event fan-out.
  - **No Shared VHCI IOCTL Filtering**: Control-plane filtering (`GET_IMPORTED_DEVICES`, `PLUGOUT_HARDWARE`, `STOP_ATTACH_ATTEMPTS`) becomes inherent per controller instance.
- **What Per-User VHCIs Do *Not* Solve (Why Child PDO Isolation Remains Mandatory):**
  - **No PnP Child Inheritance from Host Controller FDOs**: In Windows PnP, child PDOs enumerated beneath a virtual USB host controller (`USB\ROOT_HUB30` → `USB\VID_...` → `usbccgp` `MI_xx` / `HidUsb` `HID\...` / `disk.sys` / `usbser.sys`) are created in the global PnP device tree (`\Device\USBPDO-N`) with class default SDDLs (`Everyone: RW`) and without `DEVPKEY_Device_SessionId` or container SDDL inheritance from the parent VHCI FDO.
  - Consequently, even with one dedicated VHCI per user, Windows still requires `usbip2_filter.sys` (child PDO `DEVPKEY_Device_SessionId` stamping, `IRP_MJ_CREATE` filtering, and stack-wide `set_device_dacl`) plus `usbip2_ude.sys` volume/disk DACL hardening and global-drive-letter redirection to prevent foreign sessions or foreign admins from opening attached child devices and storage volumes.
  - Because `usbip2_ude.inf` already allows multiple root-enumerated instances (`DICD_GENERATE_ID`), a per-user/session VHCI model can be layered cleanly on top of the current architecture in the future if a lifecycle broker service (e.g., creating controllers via `SwDeviceCreate` or logon tasks and retiring them on logoff) becomes desirable.

---

## 8. Viability Assessment
- **Is the solution viable?** **Yes.** For the primary multi-user USB/IP workloads on Windows (RDS/Terminal Server, shared workstations, Fast User Switching)—particularly **USB storage drives**, **WinUSB / vendor-specific / dongle / smartcard devices**, and **session-scoped PnP devices (`-i session`)**—this architecture delivers kernel-enforced isolation without requiring administrative elevation from end users or a privileged background Windows service.
- **Why `-i session` is the primary recommended mode**: Windows' own RDS USB redirection (`TsUsbFlt.sys`) relies on the same OS primitive (`DEVPKEY_Device_SessionId` on the USB PDO + per-session `DosDevices`). Combined with `caller_user_sid` enforcement, `usbccgp` `MI_xx` child PDO filtering, kernel volume/disk DACL hardening, and automatic teardown on logoff (`IoRegisterContainerNotification`), `-i session` aligns natively with the Windows I/O and PnP architecture.
- **Role of `-i user`**: Best suited for **storage volumes**, **direct `USB\VID_...` (WinUSB) devices**, and **boot-time persistent attachments** where ownership must survive across session logoffs or `SessionId` reassignments.
