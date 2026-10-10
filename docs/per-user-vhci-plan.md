# Layer #3 Implementation Plan: N-Dedicated Per-User VHCI Instances

## 1. Overview & Goals

This document specifies the implementation plan for **Layer #3 (N-Dedicated Per-User Virtual HCI Instances)**, replacing the single shared VHCI controller with **one dedicated virtual USB Host Controller (`USBIP_WIN2\UDE`) per user**, spawned on demand by a lightweight root-enumerated bus FDO (`ROOT\USBIP_WIN2\BUS`) inside `usbip2_ude.sys`.

### 1.1 Key Design Principles (Why N-Dedicated Only, No Shared VHCI)

Eliminating the user-facing Shared VHCI so that every user interacts exclusively with their own dedicated VHCI instance avoids all dual-controller multiplexing in userspace:
1. **Single `HANDLE` per User (`vhci::open()`)**: `usbip::vhci::open()` opens (or triggers on-demand creation of) the calling user's own dedicated VHCI and returns a single Win32 `HANDLE`. The public C++17 `libusbip` SDK ABI ([userspace/libusbip/vhci.h](file:///D:/usbip-win2/userspace/libusbip/vhci.h)), `usbip.exe`, and `wusbip.exe` (single `ReadFile` event thread and single `get_vhci()` handle) remain unchanged.
2. **1:1 Hardware Port Numbers (`1..60`)**: Because each user only addresses their own VHCI, roothub port numbers remain `1..60`, matching `DEVPKEY_Device_Address` and `vhci_ctx::devices[port - 1]` 1:1 without synthetic port offsets (`101..299`).
3. **All Three Isolation Modes (`-i none`, `-i session`, `-i user`) Work on the User's Dedicated VHCI**:
   - **Control plane (`CreateFile` on the VHCI)**: Always restricted to `SYSTEM`, `Administrators`, and `<UserSID>` by the Windows Object Manager via the VHCI device DACL.
   - **Data plane (child USB PDOs & storage volumes)**:
     - `-i none`: Child USB PDOs and storage volumes are **not** isolated (`usbip2_filter.sys` and `usbip2_ude.sys` skip `DEVPKEY_Device_SessionId`, `set_device_dacl()`, and volume isolation), so the attached USB device is usable by all users/sessions on the machine while only the owning user (or an Administrator) can detach it.
     - `-i session`: Child USB PDOs and volumes are isolated to the caller's interactive `SessionId` + `<UserSID>`, and `IoRegisterContainerNotification` automatically detaches `-i session` devices when that session logs off.
     - `-i user`: Child USB PDOs and volumes are isolated to `<UserSID>` across session logoffs, RDP reconnects, and Fast User Switching.

### 1.2 What Layer #3 Adds (and What Still Relies on Layers #1, #2A, and Volume Hardening)

| Isolation Dimension | Provided by Layer #3 (N-Dedicated VHCIs) | Still Required from Layers #1, #2A & Storage Isolation |
|---|---|---|
| **VHCI Control-Plane Handle (`CreateFile`)** | Enforced by Windows Object Manager via per-VHCI DACL (`ERROR_ACCESS_DENIED` for foreign users before any IOCTL reaches `usbip2_ude.sys`) | N/A |
| **Roothub Port Pool (`UDECXUSBDEVICE[]`)** | Dedicated 60-port array (`NumberOfUsb20Ports` + `NumberOfUsb30Ports`) per user; zero cross-user port contention | Global `MaxVhciInstances` cap on the Bus FDO prevents VHCI-spawning DoS |
| **Event Stream (`IRP_MJ_READ`) & Reattach Queue** | Physically separate `vhci_ctx::fileobjects`, `reads` queue, and `reattach_req` collection per user VHCI | N/A |
| **Child USB, `usbccgp` `MI_xx`, & `HidUsb` `HID\...` PDOs** | Enumerated under the user's dedicated `USB\ROOT_HUB30` | **Mandatory**: Windows PnP does **not** inherit parent VHCI DACLs or `SessionId` onto child PDOs; `usbip2_filter.sys` must still stamp `DEVPKEY_Device_SessionId`, apply stack-wide `set_device_dacl()`, and filter `IRP_MJ_CREATE` |
| **Storage Volumes & Drive Letters** | Disk/volume stack parented under the user's dedicated VHCI | **Mandatory**: `MountMgr` and `volmgr.sys` are global; `usbip2_ude.sys` and `libusbip` must still harden `\Device\HarddiskVolumeN` & `\Device\HarddiskN\DR0` DACLs, unlink `\GLOBAL??\X:`, and map session-local `DosDevices` drive letters |

---

## 2. Dynamic VHCI Creation Architecture

### 2.1 Why a Separate Non-UdeCx Bus FDO Is Required

In `usbip2_ude.sys`, calling `UdecxInitializeWdfDeviceInit()` ([drivers/ude/vhci.cpp](file:///D:/usbip-win2/drivers/ude/vhci.cpp#L680)) and `UdecxWdfDeviceAddUsbDeviceEmulation()` ([drivers/ude/vhci.cpp](file:///D:/usbip-win2/drivers/ude/vhci.cpp#L703)) binds a `WDFDEVICE` to `UdeCx.sys` / `ucx01000.sys`, which takes ownership of `IRP_MN_QUERY_DEVICE_RELATIONS` (`BusRelations`) on that FDO to enumerate its virtual `USB\ROOT_HUB30` PDO. Furthermore, `WdfDeviceInitAssignSDDLString()` runs inside `EvtDriverDeviceAdd` on a `SYSTEM` PnP worker thread rather than the calling user's thread.

### 2.2 Two-Role `usbip2_ude.sys` (Bus FDO + N Per-User VHCI FDOs)

To allow standard, non-elevated users to obtain their dedicated VHCI on demand **without** a user-mode Windows service or UAC elevation:

```
ROOT\USBIP_WIN2\BUS (Root-enumerated Bus FDO in usbip2_ude.sys, installed once by setup.iss)
  +-- Exposes GUID_DEVINTERFACE_USBIP_BUS (Everyone: RW, accepts IOCTL_ENSURE_USER_VHCI)
  |
  +-- Child PDO #1: USBIP_WIN2\UDE (Dedicated VHCI for User A SID)
  |     +-- Device DACL: SYSTEM (GA), Administrators (GA), User A SID (GA)
  |     +-- Interface: GUID_DEVINTERFACE_USBIP_VHCI with ReferenceString L"<UserA_SID_Hex>"
  |     +-- Reattaches PersistentDevices entries matching User A SID
  |     +-- USB\ROOT_HUB30 (filtered by usbip2_filter.sys, ports 1..60)
  |
  +-- Child PDO #2: USBIP_WIN2\UDE (Dedicated VHCI for User B SID)
        +-- Device DACL: SYSTEM (GA), Administrators (GA), User B SID (GA)
        +-- Interface: GUID_DEVINTERFACE_USBIP_VHCI with ReferenceString L"<UserB_SID_Hex>"
        +-- Reattaches PersistentDevices entries matching User B SID
        +-- USB\ROOT_HUB30 (filtered by usbip2_filter.sys, ports 1..60)
```

1. **INF & Installer ([drivers/ude/usbip2_ude.inf](file:///D:/usbip-win2/drivers/ude/usbip2_ude.inf#L28-L30), [userspace/innosetup/setup.iss](file:///D:/usbip-win2/userspace/innosetup/setup.iss#L158))**:
   - In `usbip2_ude.inf`, match both root-enumerated `ROOT\USBIP_WIN2\BUS` and child-enumerated `USBIP_WIN2\UDE`:
     ```ini
     [Standard.NT$ARCH$.10.0...17763]
     %BusDisplayName% = ude_Device, ROOT\USBIP_WIN2\BUS
     %DisplayName%    = ude_Device, USBIP_WIN2\UDE
     ```
   - `setup.iss` installs the single root devnode `ROOT\USBIP_WIN2\BUS` via `devnode.exe`.
2. **Role Dispatch in `usbip::DeviceAdd` ([drivers/ude/vhci.cpp](file:///D:/usbip-win2/drivers/ude/vhci.cpp#L1215-L1233))**:
   - Query the PDO hardware ID (`WdfFdoInitAllocAndQueryProperty(..., DevicePropertyHardwareID, ...)`):
     - If `ROOT\USBIP_WIN2\BUS`: create the **Bus FDO** (`create_bus_fdo`) with a `WDFCHILDLIST` (`WdfFdoInitSetDefaultChildListConfig`), register `GUID_DEVINTERFACE_USBIP_BUS` with `SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R`, and pre-spawn child VHCIs for any user SIDs present in boot-time `PersistentDevices`.
     - If `USBIP_WIN2\UDE`: create a dedicated UdeCx **VHCI FDO** (`create_vhci`) bound to the `owner_sid` stored in its child PDO identification description.
3. **On-Demand Creation via `IOCTL_ENSURE_USER_VHCI`**:
   - When an unprivileged process calls `usbip::vhci::open()`, if no `GUID_DEVINTERFACE_USBIP_VHCI` with `ReferenceString == <CallerSidHex>` exists yet, `libusbip` opens `GUID_DEVINTERFACE_USBIP_BUS` and sends `IOCTL_ENSURE_USER_VHCI`.
   - The Bus FDO:
     - Extracts the caller's effective `TokenUser` SID (`libdrv::get_requestor_sid(irp, caller_sid)`).
     - Checks if a child VHCI for `caller_sid` is already present; if not, verifies the active child count is below `MaxVhciInstances` (default `32`, configurable in `Parameters`).
     - Populates a `vhci_child_id` identification description (`WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER`, `sid_blob owner_sid`) and calls `WdfChildListAddOrUpdateChildDescriptionAsPresent`.
     - Returns `caller_sid` (or its hex reference string) so `libusbip` can open the user's `GUID_DEVINTERFACE_USBIP_VHCI` symbolic link as soon as PnP completes `DeviceAdd`.

---

## 3. Required Kernel Changes

### 3.1 `drivers/ude/` (`usbip2_ude.sys`)

1. **Per-VHCI Owner Identity ([drivers/ude/context.h](file:///D:/usbip-win2/drivers/ude/context.h#L38-L67))**:
   - Add to `struct vhci_ctx`:
     ```cpp
     sid_blob owner_sid;               // User SID owning this dedicated VHCI instance
     wchar_t interface_ref_buf[140];   // ReferenceString "<UserSidHex>" for GUID_DEVINTERFACE_USBIP_VHCI
     UNICODE_STRING interface_ref;
     ```
2. **Per-User VHCI Device DACL & Interface Registration ([drivers/ude/vhci.cpp](file:///D:/usbip-win2/drivers/ude/vhci.cpp#L407-L425))**:
   - During `initialize()` ([drivers/ude/vhci.cpp](file:///D:/usbip-win2/drivers/ude/vhci.cpp#L674)), assign `SDDL_DEVOBJ_SYS_ALL_ADM_ALL` so the device is protected during creation before the user DACL is applied.
   - In `create_vhci()`, apply a protected DACL granting `GENERIC_ALL` to `SYSTEM`, `Administrators` (so elevated admins can still inspect/detach stuck devices), and `ctx.owner_sid` (omitting `Everyone`).
   - Register `WdfDeviceCreateDeviceInterface(vhci, &vhci::GUID_DEVINTERFACE_USBIP_VHCI, &ctx.interface_ref)` where `ctx.interface_ref` is the hex string of `ctx.owner_sid`.
   - Because the entire 60-port roothub belongs to `ctx.owner_sid`, `ctx.max_user_ports` equals `ctx.devices_cnt`.
3. **Per-Owner Boot-Time Persistent Reattach ([drivers/ude/persistent.cpp](file:///D:/usbip-win2/drivers/ude/persistent.cpp#L814-L838), [drivers/ude/vhci.cpp](file:///D:/usbip-win2/drivers/ude/vhci.cpp#L1229))**:
   - In `plugin_persistent_devices(vhci)`: filter parsed entries from `PersistentDevices` so a VHCI instance only attaches persistent devices whose `user_sid` matches `get_vhci_ctx(vhci)->owner_sid` (or empty `user_sid` if `owner_sid` is `SeLocalSystemSid`).

---

### 3.2 `drivers/ude_filter/` (`usbip2_filter.sys`)

1. **Pair Each `USB\ROOT_HUB30` FiDO with Its Own Parent VHCI ([drivers/ude_filter/device.cpp](file:///D:/usbip-win2/drivers/ude_filter/device.cpp#L112-L154))**:
   - Today, `get_vhci_device()` calls `get_device_interfaces(list, vhci::GUID_DEVINTERFACE_USBIP_VHCI)` and picks the first VHCI in `list`. With N Dedicated VHCIs, Root Hub #K must query `INTERNAL_GET_PORT_ISOLATION` on its own parent VHCI #K:
     1. Query `DEVPKEY_Device_Parent` on `fltr.pdo` (`USB\ROOT_HUB30`) via `IoGetDevicePropertyData`, which returns the parent VHCI's `InstanceId` (e.g., `USBIP_WIN2\UDE\...`).
     2. Iterate the symbolic links returned by `get_device_interfaces(list, vhci::GUID_DEVINTERFACE_USBIP_VHCI)`.
     3. For each link, call `IoGetDeviceObjectPointer(&link, FILE_READ_DATA, &file, &dev)`, obtain the base PDO of that VHCI stack via `base_pdo = IoGetDeviceAttachmentBaseRef(dev)`, query `DEVPKEY_Device_InstanceId` on `base_pdo` (remembering `ObDereferenceObject(base_pdo)`), and compare it case-insensitively (`RtlEqualUnicodeString(..., true)`) with `fltr.pdo`'s `DEVPKEY_Device_Parent`.
     4. Cache the matched `dev` in `fltr.hub.vhci` and dereference non-matching candidates.
   - Because `get_vhci_device()` executes during `IRP_MN_START_DEVICE` on a `KernelMode` / `SYSTEM` worker thread, `IoGetDeviceObjectPointer` always succeeds against any user's dedicated VHCI.

---

## 4. Required Userspace Changes (`userspace/`)

### 4.1 `userspace/libusbip/src/vhci.cpp` — Opening the Caller's Dedicated VHCI

In [userspace/libusbip/src/vhci.cpp](file:///D:/usbip-win2/userspace/libusbip/src/vhci.cpp#L214-L256), only `get_path()` changes:
1. Resolve the calling thread/process `TokenUser` SID into its uppercase hex string `<CallerSidHex>` (using existing helpers in [userspace/libusbip/src/sid.h](file:///D:/usbip-win2/userspace/libusbip/src/sid.h)).
2. Enumerate present `GUID_DEVINTERFACE_USBIP_VHCI` paths via `CM_Get_Device_Interface_List` and look for the path ending with `\<CallerSidHex>`.
3. If not found:
   - Open `GUID_DEVINTERFACE_USBIP_BUS`, send `IOCTL_ENSURE_USER_VHCI`, and poll `CM_Get_Device_Interface_List` briefly (up to ~2 seconds, 25 ms interval) until the caller's `\<CallerSidHex>` VHCI interface arrives.
4. Return that single device interface path from `get_path()`.
   - **Result**: `usbip::vhci::open()`, `attach()`, `detach()`, `get_imported_devices()`, `stop_attach_attempts()`, `read_device_state()`, `usbip.exe`, and `wusbip.exe` all work without signature or port-numbering changes.

### 4.2 `userspace/libusbip/src/volume_isolation.cpp` — Matching the Caller's VHCI `DEVINST`

In [userspace/libusbip/src/volume_isolation.cpp](file:///D:/usbip-win2/userspace/libusbip/src/volume_isolation.cpp#L185-L208), update `get_vhci_devinst()`:
1. Instead of returning `SetupDiEnumDeviceInfo(di.get(), 0, &dd)` (which hardcodes index `0`), iterate the enumerated `GUID_DEVINTERFACE_USBIP_VHCI` interfaces (`SetupDiEnumDeviceInterfaces` + `get_device_interface_path`).
2. Match the interface path ending with `\<CallerSidHex>` and return its `SP_DEVINFO_DATA::DevInst`.
3. `get_disk_port_number(disk_devinst, vhci_devinst)` and `ISOLATE_PORT_VOLUME` continue to use the 1-based hardware port number (`1..60`) directly.

---

## 5. Step-by-Step Execution & Verification Sequence

| Phase | Files Touched | Deliverable & Verification |
|---|---|---|
| **Phase 1: Bus FDO & Child VHCI Enumeration** | `drivers/ude/usbip2_ude.inf`, `drivers/ude/vhci.h`, `drivers/ude/vhci.cpp`, `drivers/ude/context.h`, `drivers/ude/persistent.cpp`, `include/usbip/vhci.h`, `userspace/innosetup/setup.iss` | Add `ROOT\USBIP_WIN2\BUS` Bus FDO + `USBIP_WIN2\UDE` child VHCI enumeration (`WDFCHILDLIST`), `IOCTL_ENSURE_USER_VHCI`, per-user VHCI DACL (`SYSTEM`, `Administrators`, `owner_sid`), `<UserSidHex>` `ReferenceString`, and per-owner `plugin_persistent_devices`. |
| **Phase 2: `ude_filter` Parent VHCI Matching** | `drivers/ude_filter/device.cpp` | Update `get_vhci_device()` to match `fltr.pdo` (`USB\ROOT_HUB30`) `DEVPKEY_Device_Parent` against each candidate VHCI's base PDO `DEVPKEY_Device_InstanceId`. |
| **Phase 3: `libusbip` Per-User Interface Resolution** | `userspace/libusbip/src/vhci.cpp`, `userspace/libusbip/src/volume_isolation.cpp` | Update `get_path()` to match `\<CallerSidHex>` and issue `IOCTL_ENSURE_USER_VHCI` on demand; update `get_vhci_devinst()` to select the `DEVINST` of the caller's `\<CallerSidHex>` VHCI. |
| **Phase 4: Build & Multi-User Verification** | Solution-wide (`x64` & `ARM64`) | Verify `msbuild usbip_win2.slnx /p:Configuration=Release` on `x64` and `ARM64`, verify `libusbip_check`, and test concurrent user sessions each spawning and operating their own 60-port dedicated VHCI. |
