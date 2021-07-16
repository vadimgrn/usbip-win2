#pragma once

//
// Define the tracing flags.
//
// Tracing GUID - 99b7a5cf-7cb0-4af5-838d-c45d78e49101
//

#define WPP_CONTROL_GUIDS                                              \
    WPP_DEFINE_CONTROL_GUID(                                           \
        vhciTraceGuid, (99b7a5cf,7cb0,4af5,838d,c45d78e49101),         \
                                                                       \
        WPP_DEFINE_BIT(MYDRIVER_ALL_INFO)                              \
        WPP_DEFINE_BIT(DRIVER)                                         \
        WPP_DEFINE_BIT(VHCI)                                           \
        WPP_DEFINE_BIT(QUEUE_HC)                                       \
        WPP_DEFINE_BIT(VUSB)                                           \
        WPP_DEFINE_BIT(READ)                                           \
        WPP_DEFINE_BIT(WRITE)                                          \
        WPP_DEFINE_BIT(EP)                                             \
        WPP_DEFINE_BIT(QUEUE_EP)                                       \
        WPP_DEFINE_BIT(URBR)                                           \
        WPP_DEFINE_BIT(IOCTL)                                          \
        WPP_DEFINE_BIT(PLUGIN)                                         \
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
// defined below which sets FLAGS=MYDRIVER_ALL_INFO, a custom macro must be defined to
// reorder the arguments to what the .tpl configuration file expects.
//
#define WPP_RECORDER_FLAGS_LEVEL_ARGS(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_ARGS(lvl, flags)
#define WPP_RECORDER_FLAGS_LEVEL_FILTER(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_FILTER(lvl, flags)

//
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC Trace{FLAGS=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//
