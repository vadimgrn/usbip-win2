#pragma once

//
// Define the tracing flags.
//
// Tracing GUID - 8b56380d-5174-4b15-b6f4-4c47008801a4
//

#define WPP_CONTROL_GUIDS                                                 \
    WPP_DEFINE_CONTROL_GUID(                                              \
        UsbipVhciTraceGuid, (8b56380d,5174,4b15,b6f4,4c47008801a4),       \
                                                                          \
        WPP_DEFINE_BIT(TRACE_GENERAL)           /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(TRACE_READ)              /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(TRACE_WRITE)             /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(TRACE_PNP)               /* bit  3 = 0x00000008 */ \
        WPP_DEFINE_BIT(TRACE_IOCTL)             /* bit  4 = 0x00000010 */ \
        WPP_DEFINE_BIT(TRACE_POWER)             /* bit  5 = 0x00000020 */ \
        WPP_DEFINE_BIT(TRACE_WMI)               /* bit  6 = 0x00000040 */ \
        WPP_DEFINE_BIT(TRACE_URB)               /* bit  7 = 0x00000080 */ \
        WPP_DEFINE_BIT(TRACE_VDEV)              /* bit  8 = 0x00000100 */ \
        WPP_DEFINE_BIT(TRACE_ROOT)              /* bit  9 = 0x00000200 */ \
        WPP_DEFINE_BIT(TRACE_VHCI)              /* bit 10 = 0x00000400 */ \
        WPP_DEFINE_BIT(TRACE_CPDO)              /* bit 11 = 0x00000800 */ \
        WPP_DEFINE_BIT(TRACE_HPDO)              /* bit 12 = 0x00001000 */ \
        WPP_DEFINE_BIT(TRACE_VHUB)              /* bit 13 = 0x00002000 */ \
        WPP_DEFINE_BIT(TRACE_VPDO)              /* bit 14 = 0x00004000 */ \
        )                             

#define WPP_FLAG_LEVEL_LOGGER(flag, level)                                  \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level)                                 \
    (WPP_LEVEL_ENABLED(flag) &&                                             \
     WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) \
           WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//           
// WPP orders static parameters before dynamic parameters. To support the Trace function
// defined below which sets FLAGS=TRACE_GENERAL, a custom macro must be defined to
// reorder the arguments to what the .tpl configuration file expects.
//
#define WPP_RECORDER_FLAGS_LEVEL_ARGS(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_ARGS(lvl, flags)
#define WPP_RECORDER_FLAGS_LEVEL_FILTER(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_FILTER(lvl, flags)

//
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC Trace{FLAGS=TRACE_GENERAL}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//
