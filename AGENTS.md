# Agents instructions for usbip-win2

## Architecture Overview

**usbip-win2** is a Windows USB/IP client implementation that uses modern C++23 and the Windows Driver Framework (WDF). The codebase is split into three main layers:

### Driver Layer (`drivers/`)
- **libdrv/**: Common kernel-mode library providing RAII wrappers around Windows Driver APIs (WDF, WSK, MDL, IRPs)
- **ude/**: USB Device Emulation (UDE) driver - the main USB/IP client driver using Winsock Kernel NPI (KMDF driver)
- **ude_filter/**: Companion upper filter driver for device-specific handling (WDM driver)
- **package/**: Driver packaging and signing project

### Userspace Layer (`userspace/`)
- **libusbip/**: Public SDK/DLL for USB/IP client functionality (public API uses C++17, implementation uses C++23); includes networking, USB device management, and helper utilities
- **usbip/**: Command-line utility for attaching/detaching remote USB devices
- **wusbip/**: wxWidgets-based GUI application for device management
- **devnode/**: Device node utility for driver management
- **resources/**: Shared resource DLL for messages and strings

### Testing & Build
- **libusbip_check/**: Compile-time test ensuring C++17 API compatibility
- **vcpkg/**: Dependency management (wxWidgets, pcre2, etc.)

## Build System

### Prerequisites
- Visual Studio 2026 with "Windows Driver Kit" component
- vcpkg (automatically handled by `bootstrap.bat`)
- C++23 standard library (via NuGet)

### Building
1. Run `bootstrap.bat` to initialize vcpkg and git submodules
2. Open `usbip_win2.slnx` in Visual Studio (or build via `msbuild usbip_win2.slnx /p:Configuration=Release /p:Platform=x64`)
3. For driver signing: Right-click "package" project > Properties > Driver Signing > Test Certificate (password: `usbip`)
4. Build solution (Ctrl+Shift+B or Build > Build Solution)

Build artifacts go to:
- `x64/Debug/` and `x64/Release/` for x64 builds
- `ARM64/Debug/` and `ARM64/Release/` for ARM64 builds

### No Traditional Unit Tests
The project uses `libusbip_check` as a **compile-time validation** tool (not a runtime test suite). It verifies the C++17 API compatibility by attempting to link against the public API headers.

## Code Conventions

### Namespaces & Organization
- Driver code uses `wdf::`, `wdm::`, `wsk::` namespaces for Windows API abstractions
- Userspace code primarily uses `usbip::` namespace; never introduce `using namespace ...` into `namespace std` (e.g., when specializing `std::hash`)
- Header files in top-level directory; implementation in `src/` subdirectories
- `#pragma once` for header guards (not `#ifndef` guards)
- Use forward slashes `/` in `#include` directives (e.g., `#include <libdrv/remove_lock.h>`), never backslashes `\`

### Naming
- **Structs/Classes**: `CamelCase` (e.g., `ObjectRef`, `usb_device`, `usb_interface`)
- **Free functions**: `snake_case` (e.g., `byteswap_header()`, `get_payload_size()`)
- **Constants/Enums**: `snake_case` with prefix or `CamelCase` depending on context
- **Windows API types**: Use standard types (`UINT32`, `LPVOID`, `WDFOBJECT`, etc.)

### RAII Patterns
- Kernel: `ObjectRef`, `unique_ptr`, `auto_ref_ptr` for handle management
- Spinlocks / WaitLocks: Always use named instances (e.g., `wdf::spinlock lck(spin_lock);` or `wdf::waitlock lck(wait_lock);`). Unnamed temporaries (`wdf::spinlock(spin_lock);` or `wdf::waitlock(wait_lock);`) destruct immediately at the statement semicolon, leaving code unprotected.
- Lock / Adopt / Unlock Pattern (`remove_lock_guard`):
  - In asynchronous IRP forwarding, acquire the remove lock in dispatch (`remove_lock_guard lck(lock, irp);`), disarm via `lck.clear()` before passing down the stack, and adopt in the completion routine.
  - An unnamed temporary (`libdrv::remove_lock_guard{lock, libdrv::adopt_lock, tag};`) is the intentional, idiomatic pattern to adopt an outstanding lock and release it immediately at statement completion when no further processing is needed.
  - A named instance (`remove_lock_guard lck(lock, adopt_lock, tag);`) is used when the completion routine performs post-processing and may forward ownership (`tag = lck.clear();`) or release on scope exit.
- Temporary RAII for Immediate Deallocation: Constructing an unnamed temporary wrapper around a raw pointer (e.g., `unique_ptr{raw_ptr};`) is the idiomatic pattern to take ownership of a raw heap allocation and immediately free it via its destructor at statement completion without calling manual `ExFreePool` APIs.
- Userspace: Similar patterns with `generic_handle<>`, `HKey`, `HModule` for resource ownership
- No manual `AddRef`/`Release` in wrapper classes - handled by destructors

### Documentation
- Copyright headers in all files: `/* * Copyright (c) YYYY Vadym Hrynchyshyn */`
- Doxygen-style comments: `/** @return ... */` for public APIs
- SAL annotations: `_In_`, `_Out_`, `_Inout_` for pointer parameters

### Modern C++ Features
- `constexpr` and `explicit` used liberally for optimization and type safety
- `noexcept` is used in **userspace only**; **do NOT use `noexcept` in driver code (`drivers/`)**
- `libusbip` public API uses C++17, the implementation uses C++23
- Range-based for loops, move semantics (use `static_cast<T&&>()` in drivers; `std::move` is userspace only), lambda functions where applicable
- Indent should be eight spaces
- Do not inherit from concrete types like `std::array`, `std::string`, etc.
- Use C++23 "deducing this" (explicit object parameter) for member accessors and operators to collapse redundant `const` and non-`const` overloads into a single implementation (e.g., `constexpr auto& get(this auto&& self) { return self.m_val; }`)

## Key Dependencies

- **WDF/WDK**: Windows Driver Framework (via NuGet)
- **WSK**: Winsock Kernel for kernel-mode networking
- **wxWidgets**: GUI framework (via vcpkg)
- **CLI11**: Command-line parsing library (in `userspace/CLI11/`)
- **Inno Setup**: Version 7.1+ (via NuGet package `Tools.InnoSetup`) for packaging x64 and ARM64 installers with native Pascal script uninstaller
- **vcpkg**: x64 and ARM64 triplets: `x64-windows-static-md` and `arm64-windows-static-md`

## Common Tasks

### Adding a new kernel-mode helper function
1. Add to appropriate header in `drivers/libdrv/` (e.g., `utils.h`, `wdf_cpp.h`)
2. Implement in corresponding `.cpp` file
3. Use WDF macros for error handling and code segmentation attributes
4. Document with SAL annotations

### Adding a new userspace API
1. Define in public header under `userspace/libusbip/` (e.g., `userspace/libusbip/remote.h`)
2. Implement in `userspace/libusbip/src/`
3. Export via `USBIP_API` macro (defined in `dllspec.h`)
4. Add to `libusbip_check/main.cpp` if public C++ API for compilation validation

### Building a single project
- Right-click project > Build (or use Ctrl+Shift+B on selected project)
- Output goes to architecture-specific folder (e.g., `x64/Debug/`)

### Debugging drivers
- **Event Tracing for Windows (ETW)**: WPP Software Tracing configured in projects
- **Logs**: See README.md for `tracelog`/`tracefmt` instructions
- **Kernel Debugger**: Connect WinDbg to live system or analyze crash dumps
- **Driver Verifier**: Available for additional validation (see README.md)

## Recommended MCP Server Configuration

For optimal Copilot assistance, configure the following MCP servers:

### Git Server
Enables querying commit history, branch information, and blame data:
- Use to understand why changes were made and find related commits
- Helpful for tracing features across multiple projects/drivers

### Bash Server
Enables running build and validation commands:
- Build individual projects: `msbuild userspace/libusbip/libusbip.vcxproj /p:Configuration=Release`
- Run libusbip_check for API validation
- Query project structure with PowerShell

## Important Notes

- **Driver Frameworks**: `drivers/ude` is a **KMDF driver**; `drivers/ude_filter` is a **WDM driver**
- **C++ Standards**: `libusbip` public API uses C++17, while the implementation uses C++23 (compile-time verified by `libusbip_check`)
- **Kernel vs. Userspace**: Code in `drivers/` uses kernel APIs and must follow driver safety rules (no heap allocation without lookaside lists, proper IRQL handling, etc.)
- **ARM64 Support**: Project supports both x64 and ARM64; always test on both architectures when possible
- **Test-signed Drivers**: End users must enable test signing mode (`bcdedit /set testsigning on`) after installation
- **Zero-copy Optimization**: Driver uses Memory Descriptor Lists (MDLs) and vectored I/O extensively—maintain these patterns when modifying I/O paths

### Driver Development Rules (`drivers/`)

- **Driver Frameworks**:
  - `drivers/ude` is a **KMDF driver**
  - `drivers/ude_filter` is a **WDM driver**
- **No C++ Standard Library (`std::`)**: Drivers compile with `/kernel`. STL headers (`<utility>`, `<memory>`, `<algorithm>`, `<vector>`, `<string>`, `<functional>`, `<type_traits>`, `<ranges>`, `<span>`, etc.) are not available or permitted.
  - Do NOT use `std::move` — use `static_cast<T&&>(val)`.
  - Do NOT use `std::swap` — use `::swap` from `drivers/libdrv/utils.h` or class-specific `swap()`.
  - Do NOT use `std::unique_ptr` — use `libdrv::unique_ptr_t` (from `drivers/libdrv/unique_ptr.h`) with appropriate pool tags.
  - Do NOT use STL `<type_traits>` (`std::conditional_t`, `std::is_const_v`, `std::remove_reference_t`, etc.) — driver code must use pure template techniques and intrinsics without STL metaprogramming headers.
- **No `noexcept` in Driver Code**: Do not annotate functions, constructors, destructors, or operators with `noexcept` in `drivers/`. Drivers compile in `/kernel` mode with C++ exceptions disabled.
- **No C++ Exceptions or RTTI**: `throw`, `try`, `catch`, `dynamic_cast`, and `typeid` are prohibited and disabled (`/kernel`, `/GR-`).
- **WDK C Headers Linkage (`extern "C"`)**: Legacy WDK C headers lacking internal `extern "C"` blocks (such as `<usbdlib.h>`) must be enclosed in `extern "C" { #include <header.h> }` to prevent C++ name mangling of kernel APIs.
- **Driver IRQL Contracts & SAL**: Always annotate driver functions with SAL IRQL contracts (`_IRQL_requires_same_`, `_IRQL_requires_max_(DISPATCH_LEVEL)` or `PASSIVE_LEVEL`). Observe IRQL limits (no paging or blocking at `DISPATCH_LEVEL`).
- **Memory Management & Synchronization**:
  - No global `operator new`/`delete` or heap allocations (`malloc`/`free`). Use `ExAllocatePoolZero`, `ExAllocatePoolUninitialized`, or lookaside lists.
  - Always use named RAII lock instances for mutual exclusion (e.g., `wdf::spinlock lck(spin_lock);` or `wdf::waitlock lck(wait_lock);`). Never use unnamed temporaries for scoped locks.
  - **Lock / Adopt / Unlock Pattern**: For asynchronous completion, acquire via `remove_lock_guard lck(lock, tag);`, transfer via `lck.clear()`, and adopt in the completion routine via unnamed `remove_lock_guard{lock, adopt_lock, tag};` (immediate release) or named `remove_lock_guard lck(lock, adopt_lock, tag);` (scoped/forwarded).
  - **Immediate Deallocation Pattern**: Use unnamed temporary wrappers (`unique_ptr{ptr};`) for idiomatic, tag-safe pool freeing.
  - **Partial MDLs & Locking**: Never call `MmUnlockPages` on a partial MDL created by `IoBuildPartialMdl`. Partial MDLs inherit `MDL_PAGES_LOCKED` from their source MDL; unlocking a partial MDL prematurely decrements physical page lock counts and triggers Driver Verifier bugchecks (`0xC4` / `PFN_SHARE_COUNT`).
  - **WaitLock Status Handling**: When checking the result of `WdfWaitLockAcquire`, verify `status == STATUS_SUCCESS`. Do NOT use `NT_SUCCESS(status)` because `NT_SUCCESS(STATUS_TIMEOUT)` evaluates to `TRUE` (`0x00000102`), which would falsely indicate the lock was acquired.
- **NTSTATUS Severity & Error Handling (`NT_SUCCESS` vs. `NT_ERROR`)**:
  - `NTSTATUS` severity encoding (bits 31:30): `00` = Success (`0x00000000`–`0x3FFFFFFF`), `01` = Informational/Notice (`0x40000000`–`0x7FFFFFFF` and `STATUS_TIMEOUT` `0x00000102`), `10` = Warning (`0x80000000`–`0xBFFFFFFF`), `11` = Error (`0xC0000000`–`0xFFFFFFFF`).
  - **Do NOT use `NT_ERROR` for failure checks**: `NT_ERROR(status)` evaluates only severity `11`. It evaluates to `FALSE` for Warning (`10`) and Informational (`01`). Routines such as `RtlStringCbCopyNA`, `RtlStringCbPrintfExA`, `RtlUnicodeStringPrintf`, and `WdfRegistryQueryMultiString` return `STATUS_BUFFER_OVERFLOW` (`0x80000005`, warning) on truncation/insufficient buffer. Using `NT_ERROR` silently ignores the warning and treats truncation as success. Always use `!NT_SUCCESS(status)` to detect failures.
  - **Be vigilant with `NT_SUCCESS` on functions returning notices/timeouts**: `NT_SUCCESS(status)` evaluates `(status >= 0)`, which is `TRUE` for Informational/Notice codes. Routines like `WskCaptureProviderNPI`, `WdfWaitLockAcquire`, and `KeWaitForSingleObject` return `STATUS_TIMEOUT` (`0x00000102`) when a timeout occurs; do NOT use `NT_SUCCESS` to check them—verify `status == STATUS_SUCCESS`.
  - **Handle warnings for buffer sizing routines**: Routines such as `WskControlClient` and `WskControlSocket` return `STATUS_BUFFER_OVERFLOW` when the output buffer is too small and populate the required buffer size in `IoStatus.Information`. Checking only `NT_SUCCESS` drops the valid size because `NT_SUCCESS(STATUS_BUFFER_OVERFLOW)` is `FALSE`; explicitly allow `(NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW)`.
- **String Handling & CRT Function Prohibition (No `str*` / `wcs*` functions; use `<ntstrsafe.h>`)**:
  - **Do NOT use CRT string functions (`str*` / `wcs*`) in driver code**: Any standard C runtime string manipulation or comparison functions (such as `strcpy`, `strncpy`, `strcat`, `strncat`, `strlen`, `strcmp`, `strncmp`, `sprintf`, `snprintf`, etc.) must NOT be used in driver code. They rely on null terminators (`\0`) and do not provide kernel safety guarantees. If an untrusted caller (e.g., from user-mode IOCTL buffers or network PDUs) provides a malformed string lacking a null terminator, CRT functions can read or write past buffer boundaries, resulting in information disclosure, memory corruption, or bugchecks (BSOD). Furthermore, CRT string routines are not designed for kernel IRQL or SIMD-optimized memory comparison.
  - **Only use safe string functions from `<ntstrsafe.h>`**: All string manipulation and length calculations in driver code must use routines from `<ntstrsafe.h>` (e.g., `RtlStringCbCopyNA`, `RtlStringCbPrintfA`, `RtlStringCbLengthA`, `RtlStringCchLengthA`, etc.), which enforce explicit buffer length limits and return `NTSTATUS`.
  - **String equality comparison using `RtlCompareMemory`**:
    - Because `<ntstrsafe.h>` does not provide string comparison functions, perform string equality checks safely:
      - Determine string lengths using safe bounds checking (`RtlStringCbLengthA` or `RtlStringCchLengthA`). If a string is not null-terminated within its buffer bounds, the function fails safely.
      - If the lengths of two strings do not match (`Length1 != Length2`), they cannot be identical.
      - If lengths match, safely compare the exact byte layout using `(RtlCompareMemory(String1, String2, Length) == Length)`.
      - Use the helper `libdrv::equal_strings` from `drivers/libdrv/strconv.h` for comparing bounded strings or fixed-size character arrays.



