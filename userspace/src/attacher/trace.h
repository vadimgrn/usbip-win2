#pragma once

//
// Define the tracing flags.
//
// Tracing GUID - d176d307-7219-4207-a588-5d4ed4d4e46a
//

#define WPP_CONTROL_GUIDS                                                \
    WPP_DEFINE_CONTROL_GUID(                                             \
        UsbipAttacherGuid, (d176d307,7219,4207,a588,5d4ed4d4e46a),       \
                                                                         \
        WPP_DEFINE_BIT(FLAG_GENERAL)					 \
        )                             

#define WPP_FLAGS_LEVEL_LOGGER(flags, level)                                  \
    WPP_LEVEL_LOGGER(flags)

#define WPP_FLAGS_LEVEL_ENABLED(flags, level)                                 \
    (WPP_LEVEL_ENABLED(flags) &&                                             \
     WPP_CONTROL(WPP_BIT_ ## flags).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) \
           WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)


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
