/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <wx/log.h>

class wxMenuItem;

enum { DEFAULT_LOGLEVEL = wxLOG_Status, VERBOSE_LOGLEVEL };

/*
 * Do not show dialog box for wxLOG_Info aka Verbose.
 */
class LogWindow : public wxLogWindow
{
public:
	LogWindow(_In_ wxWindow *parent, _In_ const wxMenuItem *log_toogle);

private:
	void DoLogRecord(_In_ wxLogLevel level, _In_ const wxString &msg, _In_ const wxLogRecordInfo &info) override;
};
