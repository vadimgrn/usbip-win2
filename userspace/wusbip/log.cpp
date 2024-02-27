/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "log.h"

#include <wx/frame.h>
#include <wx/menuitem.h>
#include <wx/persist/toplevel.h>

LogWindow::LogWindow(_In_ wxWindow *parent, _In_ const wxMenuItem *log_toggle) : 
        wxLogWindow(parent, _("Log records"), false)
{
        wxASSERT(log_toggle);

        auto acc = log_toggle->GetAccel();
        wxASSERT(acc);

        wxAcceleratorEntry entry(acc->GetFlags(), acc->GetKeyCode(), wxID_CLOSE);
        wxAcceleratorTable table(1, &entry);

        GetFrame()->SetAcceleratorTable(table);
        wxPersistentRegisterAndRestore(GetFrame(), wxString::FromAscii(__func__));
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
