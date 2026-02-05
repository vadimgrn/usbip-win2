/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "log.h"
#include "font.h"

#include <libusbip/output.h>

#include <wx/frame.h>
#include <wx/menuitem.h>
#include <wx/textctrl.h>
#include <wx/persist/toplevel.h>

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
        m_ctrl->Bind(wxEVT_MOUSEWHEEL, wxMouseEventHandler(LogWindow::on_mouse_wheel), this);

        wxPersistentRegisterAndRestore(wnd, wxString::FromAscii(__func__));

        if (wxSystemSettings::GetAppearance().IsDark()) {
                m_ctrl->SetDefaultStyle(*wxWHITE);
        }
}

wxTextCtrl* LogWindow::do_get_control()
{
        for (auto fr = GetFrame(); auto child: fr->GetChildren()) {
                if (auto ctrl = wxDynamicCast(child, wxTextCtrl)) {
                        return ctrl;
                }
        }

        wxASSERT(!"wxTextCtrl not found");
        return nullptr;
}

wxFont LogWindow::get_font() const
{
        auto &attr = m_ctrl->GetDefaultStyle();
        auto font = attr.GetFont();

        if (!font.IsOk()) { // assertion failure in wxWidgets during closing the app
                font = *wxNORMAL_FONT;
        }

        return font;
}

int LogWindow::get_font_size() const
{
        return get_font().GetPointSize();
}

bool LogWindow::set_font_size(_In_ int pt)
{
        wxTextAttr attr;
        attr.SetFontPointSize(pt);

        auto lines = m_ctrl->GetNumberOfLines();
        auto end = m_ctrl->XYToPosition(0, lines - 1);

        return m_ctrl->SetStyle(0, end, attr) && // for existing lines
               m_ctrl->SetDefaultStyle(attr); // for new lines
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

        wxAcceleratorTable table(ARRAYSIZE(entries), entries);
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

void LogWindow::change_font_size(_In_ int dir)
{
        auto font = get_font();
        usbip::change_font_size(font, dir);
        set_font_size(font.GetPointSize());
}

void LogWindow::on_font_increase(_In_ wxCommandEvent&)
{
        change_font_size(1);
}

void LogWindow::on_font_decrease(_In_ wxCommandEvent&)
{
        change_font_size(-1);
}

void LogWindow::on_font_default(_In_ wxCommandEvent&)
{
        change_font_size(0);
}

void LogWindow::on_mouse_wheel(_In_ wxMouseEvent &event)
{
        wxASSERT(event.GetEventObject() == m_ctrl);

        if (event.GetModifiers() == wxMOD_CONTROL) { // only Ctrl is depressed
                auto dir = event.GetWheelRotation();
                change_font_size(dir);
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
