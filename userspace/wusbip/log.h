/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/log.h>
#include <wx/event.h>

class wxMenuItem;

enum { DEFAULT_LOGLEVEL = wxLOG_Status, VERBOSE_LOGLEVEL };

/*
 * Do not show dialog box for wxLOG_Info aka Verbose.
 */
class LogWindow : public wxEvtHandler, public wxLogWindow
{
public:
	LogWindow(
		_In_ wxWindow *parent, 
		_In_ const wxMenuItem *log_toogle,
		_In_ const wxMenuItem *font_inc,
		_In_ const wxMenuItem *font_decr,
		_In_ const wxMenuItem *font_dflt);

        int get_font_size() const;
        bool set_font_size(_In_ int pt);

private:
        wxTextCtrl *m_ctrl = do_get_control();

        wxTextCtrl *do_get_control();
        wxFont get_font() const;
        void change_font_size(_In_ int dir);

        void DoLogRecord(_In_ wxLogLevel level, _In_ const wxString &msg, _In_ const wxLogRecordInfo &info) override;

        void on_font_increase(_In_ wxCommandEvent &event);
	void on_font_decrease(_In_ wxCommandEvent &event);
	void on_font_default(_In_ wxCommandEvent &event);
	void on_mouse_wheel(_In_ wxMouseEvent &event);

	void set_accelerators(
		_In_ const wxMenuItem *log_toogle, 
		_In_ const wxMenuItem *font_incr, 
		_In_ const wxMenuItem *font_decr, 
		_In_ const wxMenuItem *font_dflt);
};

namespace usbip
{

void enable_library_log(_In_ bool enable);
bool is_library_log_enabled();

} // namespace usbip
