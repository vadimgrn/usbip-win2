/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include "utils.h"

#include <libusbip/remote.h>

#include <wx/app.h>
#include <wx/msgdlg.h>

#include <format>

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
        if (!wxApp::OnInit()) {
                return false;
        }

        if (auto read = usbip::vhci::open(true); read && get_vhci()) {
                auto frame = new MainFrame(std::move(read));
                frame->Show(true);
                return true;
        } else {
                auto s = GetLastErrorMsg();
                wxMessageBox(s, _("Critical error"), wxICON_ERROR);
                return false;
        }
}

} // namespace


wxIMPLEMENT_APP(App);


MainFrame::MainFrame(usbip::Handle read) : 
        Frame(nullptr),
        m_read(std::move(read))
{
}

void MainFrame::on_exit(wxCommandEvent&)
{
        Close(true);
}

void MainFrame::log_last_error(const char *what, DWORD msg_id)
{
        auto s = GetLastErrorMsg(msg_id);
        auto text = wxString::Format("%s: %s", what, s);
        SetStatusText(text);
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

void MainFrame::on_idle(wxIdleEvent&)
{
        static bool once;
        if (!once) {
                once = true;
                async_read();
        }

        SleepEx(100, true);
}

bool MainFrame::async_read()
{
        if (!ReadFileEx(m_read.get(), m_read_buf.data(), DWORD(m_read_buf.size()), &m_overlapped, on_read)) {
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

void MainFrame::on_read(DWORD errcode)
{
        if (errcode != ERROR_SUCCESS) {
                log_last_error(__func__, errcode);
                return;
        }

        DWORD actual{};
        if (!GetOverlappedResult(m_read.get(), &m_overlapped, &actual, false)) {
                log_last_error("GetOverlappedResult");
                return;
        }

        assert(actual <= m_read_buf.size());

        if (usbip::device_state st; !vhci::get_device_state(st, m_read_buf.data(), actual)) {
                log_last_error("vhci::get_device_state");
        } else {
                state_changed(st);
                async_read();
        } 
}

void MainFrame::state_changed(const usbip::device_state &st)
{
        auto &loc = st.device.location;
        auto s = std::format("{}:{}/{} {}", loc.hostname, loc.service, loc.busid, vhci::get_state_str(st.state));
        SetStatusText(wxString::FromUTF8(s));
}
