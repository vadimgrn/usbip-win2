/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "log.h"
#include "wxutils.h"

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
        auto f = [] (auto wnd) { usbip::change_font_size(wnd, 1); };
        usbip::for_each_child(GetFrame(), f);
}

void LogWindow::on_font_decrease(_In_ wxCommandEvent&)
{
        auto f = [] (auto wnd) { usbip::change_font_size(wnd, -1); };
        usbip::for_each_child(GetFrame(), f);
}

void LogWindow::on_font_default(_In_ wxCommandEvent&)
{
        auto f = [] (auto wnd) { usbip::change_font_size(wnd, 0); };
        usbip::for_each_child(GetFrame(), f);
}

void LogWindow::on_mouse_wheel(_In_ wxMouseEvent &event)
{
        if (event.GetModifiers() != wxMOD_CONTROL) { // only Ctrl is depressed
                return;
        }

        auto wnd = static_cast<wxWindow*>(event.GetEventObject());
        auto f = [dir = event.GetWheelRotation()] (auto wnd) { usbip::change_font_size(wnd, dir); };

        usbip::for_each_child(wnd, f);
}

bool LogWindow::set_font_size(_In_ int pt)
{
        return usbip::set_font_size(GetFrame(), pt);
}

int LogWindow::get_font_size() const
{
        auto &frame = *GetFrame();

        if (auto &v = frame.GetChildren(); !v.empty()) {
                auto wnd = v.front();
                auto font = wnd->GetFont();
                return font.GetPointSize();
        }

        return 0;
}
