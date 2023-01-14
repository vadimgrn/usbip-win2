/*
* Copyright (C) 2021 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

//
// Defining control guids, including this is required to happen before
// including the tmh file (if the WppRecorder API is used)
//
#include <WppRecorder.h>

//
// Define the tracing flags.
//
// Tracing GUID - ed18c9c5-8322-48ae-bf78-d01d898a1562
//

#define WPP_CONTROL_GUIDS                                                \
    WPP_DEFINE_CONTROL_GUID(                                             \
        VhciTraceGuid, (ed18c9c5,8322,48ae,bf78,d01d898a1562),           \
                                                                         \
        WPP_DEFINE_BIT(FLAG_GENERAL)           /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(FLAG_USBIP)             /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(FLAG_URB)               /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(FLAG_DBG)               /* bit  3 = 0x00000008 */ \
        WPP_DEFINE_BIT(FLAG_WSK)               /* bit  4 = 0x00000010 */ \
        )                             

#define WPP_FLAG_LEVEL_LOGGER(flag, level) \
	WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level) \
	(WPP_LEVEL_ENABLED(flag) && WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(level, flags) \
	WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(level, flags) \
	(WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= level)

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
