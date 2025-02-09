/*
 * Copyright (C) 2023 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "log.h"
#include "font.h"

#include <libusbip/output.h>

#include <wx/frame.h>
#include <wx/menuitem.h>
#include <wx/persist/toplevel.h>

#include <memory>

LogWindow::LogWindow(
        _In_ wxWindow *parent, 
        _In_ const wxMenuItem *log_toggle,
        _In_ const wxMenuItem *font_incr,
        _In_ const wxMenuItem *font_decr,
        _In_ const wxMenuItem *font_dflt
) : 
        wxLogWindow(parent, _("Log records"), false)
{
        wxASSERT(log_toggle);
        wxASSERT(font_incr);
        wxASSERT(font_decr);
        wxASSERT(font_dflt);

        set_accelerators(log_toggle, font_incr, font_decr, font_dflt);
        auto wnd = GetFrame();

        wnd->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(LogWindow::on_font_increase), this, font_incr->GetId());
        wnd->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(LogWindow::on_font_decrease), this, font_decr->GetId());
        wnd->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(LogWindow::on_font_default), this, font_dflt->GetId());
        wnd->Bind(wxEVT_MOUSEWHEEL, wxMouseEventHandler(LogWindow::on_mouse_wheel), this);

        wxPersistentRegisterAndRestore(wnd, wxString::FromAscii(__func__));
}

void LogWindow::set_accelerators(
        _In_ const wxMenuItem *log_toggle,
        _In_ const wxMenuItem *font_incr,
        _In_ const wxMenuItem *font_decr,
        _In_ const wxMenuItem *font_dflt)
{
        std::unique_ptr<wxAcceleratorEntry> toggle(log_toggle->GetAccel());
        std::unique_ptr<wxAcceleratorEntry> incr(font_incr->GetAccel());
        std::unique_ptr<wxAcceleratorEntry> decr(font_decr->GetAccel());
        std::unique_ptr<wxAcceleratorEntry> dflt(font_dflt->GetAccel());

        wxAcceleratorEntry entries[] { 
                { toggle->GetFlags(), toggle->GetKeyCode(), wxID_CLOSE }, 
                { incr->GetFlags(), incr->GetKeyCode(), font_incr->GetId() }, 
                { decr->GetFlags(), decr->GetKeyCode(), font_decr->GetId() }, 
                { dflt->GetFlags(), dflt->GetKeyCode(), font_dflt->GetId() }, 
        };

        wxAcceleratorTable table(sizeof(entries)/sizeof(*entries), entries);
        GetFrame()->SetAcceleratorTable(table);
}

void LogWindow::DoLogRecord(_In_ wxLogLevel level, _In_ const wxString &msg, _In_ const wxLogRecordInfo &info)
{
        bool pass{};
        auto verbose = level == wxLOG_Info;

        if (verbose) {
                pass = IsPassingMessages();
                PassMessages(false);
        }

        wxLogWindow::DoLogRecord(level, msg, info);

        if (verbose) {
                PassMessages(pass);
        }
}

void LogWindow::on_font_increase(_In_ wxCommandEvent&)
{
        usbip::change_font_size(GetFrame(), 1);
}

void LogWindow::on_font_decrease(_In_ wxCommandEvent&)
{
        usbip::change_font_size(GetFrame(), -1);
}

void LogWindow::on_font_default(_In_ wxCommandEvent&)
{
        usbip::change_font_size(GetFrame(), 0);
}

void LogWindow::on_mouse_wheel(_In_ wxMouseEvent &event)
{
        if (event.GetModifiers() == wxMOD_CONTROL) { // only Ctrl is depressed
                auto wnd = static_cast<wxWindow*>(event.GetEventObject());
                usbip::change_font_size(wnd, event.GetWheelRotation());
        }
}

/*
 * wxLogXXX throws exception 'Incorrect format specifier' if use something like wxLogXXX(get_str()) 
 * and get_str() result contains '%'. 
 * wxLogVerbose(L"lib: " + wxString::FromUTF8(s)) can throw exception for this reason too.
 */
void usbip::enable_library_log(_In_ bool enable)
{
        libusbip::output_func_type f;

        if (enable) {
                f = [] (auto s) { wxLogVerbose(L"lib: %s", wxString::FromUTF8(s)); }; // see comments
        }

        libusbip::set_debug_output(f);
}

bool usbip::is_library_log_enabled()
{
        auto &f = libusbip::get_debug_output();
        return static_cast<bool>(f);
}
