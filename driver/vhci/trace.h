#pragma once

//
// Define the tracing flags.
//
// Tracing GUID - 8b56380d-5174-4b15-b6f4-4c47008801a4
//

#define WPP_CONTROL_GUIDS                                                \
    WPP_DEFINE_CONTROL_GUID(                                             \
        UsbipVhciTraceGuid, (8b56380d,5174,4b15,b6f4,4c47008801a4),      \
                                                                         \
        WPP_DEFINE_BIT(FLAG_GENERAL)           /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(FLAG_URB)               /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(FLAG_USBIP)             /* bit  2 = 0x00000004 */ \
        )                             

#define WPP_FLAG_LEVEL_LOGGER(flag, level) \
	WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level) \
	(WPP_LEVEL_ENABLED(flag) && WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(level, flag) \
	WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(level, flag) \
	(WPP_LEVEL_ENABLED(flag) && WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

//           
// WPP orders static parameters before dynamic parameters. To support the Trace function
// defined below which sets FLAGS=FLAG_GENERAL, a custom macro must be defined to
// reorder the arguments to what the .tpl configuration file expects.
//
#define WPP_RECORDER_FLAGS_LEVEL_ARGS(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_ARGS(lvl, flags)
#define WPP_RECORDER_FLAGS_LEVEL_FILTER(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_FILTER(lvl, flags)

//
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC Trace{FLAGS=FLAG_GENERAL}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//
