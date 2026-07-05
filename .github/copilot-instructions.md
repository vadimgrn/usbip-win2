# Copilot Instructions for usbip-win2

## Architecture Overview

**usbip-win2** is a Windows USB/IP client implementation that uses modern C++23 and the Windows Driver Framework (WDF). The codebase is split into three main layers:

### Driver Layer (`drivers/`)
- **libdrv/**: Common kernel-mode library providing RAII wrappers around Windows Driver APIs (WDF, WSK, MDL, IRPs)
- **ude/**: USB Device Emulation (UDE) driver - the main USB/IP client driver using Winsock Kernel NPI
- **ude_filter/**: Companion upper filter driver for device-specific handling
- **package/**: Driver packaging and signing project

### Userspace Layer (`userspace/`)
- **libusbip/**: Public SDK/DLL for USB/IP client functionality; includes networking, USB device management, and helper utilities
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
2. Open `usbip_win2.sln` in Visual Studio
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
- Userspace code primarily uses `usbip::` namespace
- Header files in top-level directory; implementation in `src/` subdirectories
- `#pragma once` for header guards (not `#ifndef` guards)

### Naming
- **Structs/Classes**: `CamelCase` (e.g., `ObjectRef`, `usb_device`, `usb_interface`)
- **Free functions**: `snake_case` (e.g., `byteswap_header()`, `get_payload_size()`)
- **Constants/Enums**: `snake_case` with prefix or `CamelCase` depending on context
- **Windows API types**: Use standard types (`UINT32`, `LPVOID`, `WDFOBJECT`, etc.)

### RAII Patterns
- Kernel: `ObjectRef`, `unique_ptr`, `auto_ref_ptr` for handle management
- Userspace: Similar patterns with `generic_handle<>`, `HKey`, `HModule` for resource ownership
- No manual `AddRef`/`Release` in wrapper classes - handled by destructors

### Documentation
- Copyright headers in all files: `/* * Copyright (c) YYYY Vadym Hrynchyshyn */`
- Doxygen-style comments: `/** @return ... */` for public APIs
- SAL annotations: `_In_`, `_Out_`, `_Inout_` for pointer parameters

### Modern C++ Features
- `constexpr`, `explicit`, `noexcept` used liberally for optimization
- Range-based for loops, move semantics, lambda functions where applicable
- C++23 scoped enums with underlying types: `enum class name : int { ... }`

## Key Dependencies

- **WDF/WDK**: Windows Driver Framework (via NuGet)
- **WSK**: Winsock Kernel for kernel-mode networking
- **wxWidgets**: GUI framework (via vcpkg)
- **CLI11**: Command-line parsing library (in `userspace/CLI11/`)
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

- **Kernel vs. Userspace**: Code in `drivers/` uses kernel APIs and must follow driver safety rules (no heap allocation without lookaside lists, proper IRQL handling, etc.)
- **ARM64 Support**: Project supports both x64 and ARM64; always test on both architectures when possible
- **Test-signed Drivers**: End users must enable test signing mode (`bcdedit /set testsigning on`) after installation
- **Zero-copy Optimization**: Driver uses Memory Descriptor Lists (MDLs) and vectored I/O extensively—maintain these patterns when modifying I/O paths
