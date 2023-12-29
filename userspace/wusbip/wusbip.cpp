/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include "utils.h"

#include <libusbip/remote.h>

#include <wx/app.h>
#include <wx/msgdlg.h>
#include <wx/log.h> 

namespace
{

using namespace usbip;

class App : public wxApp
{
public:
        bool OnInit() override;
};

bool App::OnInit()
{
        if (!get_vhci()) {
                auto s = GetLastErrorMsg();
                wxMessageBox(s, _("Critical error"), wxICON_ERROR);
                return false;
        }

        if (!wxApp::OnInit()) {
                return false;
        }

        auto frame = new MainFrame;
        frame->Show(true);

        return true;
}

void log_last_error(const char *what, DWORD msg_id = GetLastError())
{
        wxLogError("{}: {}", what, GetLastErrorMsg(msg_id));
}

} // namespace


wxIMPLEMENT_APP(App);

void MainFrame::on_exit(wxCommandEvent&)
{
        Close(true);
}

void MainFrame::on_list(wxCommandEvent&)
{
        wxMessageBox(__func__);
}

void MainFrame::on_attach(wxCommandEvent&)
{
        wxMessageBox(__func__);
}

void MainFrame::on_detach(wxCommandEvent&)
{
        wxMessageBox(__func__);
}

void MainFrame::on_port(wxCommandEvent&)
{
        wxMessageBox(__func__);
}

bool MainFrame::async_read()
{
        auto &vhci = get_vhci();

        if (!ReadFileEx(vhci.get(), m_read_buf.data(), DWORD(m_read_buf.size()), &m_overlapped, on_read)) {
                log_last_error("ReadFileEx");
                return false;
        }

        switch (auto err = GetLastError()) {
        case ERROR_SUCCESS:
        case ERROR_IO_PENDING:
                return true;
        default:
                log_last_error("ReadFileEx", err);
                return false;
        }
}

void MainFrame::on_read(DWORD errcode, DWORD /*NumberOfBytesTransfered*/, OVERLAPPED *overlapped)
{
        if ( errcode != ERROR_SUCCESS ) {
                log_last_error(__func__, errcode);
                return;
        }

        auto &self = *static_cast<MainFrame*>(overlapped->hEvent);

        DWORD actual{};
        if (auto &dev = get_vhci(); !GetOverlappedResult(dev.get(), overlapped, &actual, false)) {
                log_last_error("GetOverlappedResult");
                return;
        }

        assert(actual <= self.m_read_buf.size());

        usbip::device_state st;
        if (!vhci::get_device_state(st, self.m_read_buf.data(), actual)) {
                log_last_error("vhci::get_device_state");
                return;
        } 

        self.state_changed(st);
        self.async_read();
}

void MainFrame::state_changed(const usbip::device_state &st)
{
        auto s = usbip::vhci::get_state_str(st.state);
        SetStatusText(s);
}
