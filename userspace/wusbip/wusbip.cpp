/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include "utils.h"

#include <libusbip/remote.h>

#include <wx/app.h>
#include <wx/event.h>
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

        wxString err;

        if (auto read = init(err) ? vhci::open(true) : Handle(); !read) {
                //
        } else if (auto frame = new MainFrame(std::move(read)); frame->ok()) {
                frame->Show(true);
                return true;
        }

        if (err.empty()) {
                err = GetLastErrorMsg();
        }

        wxMessageBox(err, _("Critical error"), wxICON_ERROR);
        return false;
}

} // namespace

wxIMPLEMENT_APP(App);

class DeviceStateEvent : public wxEvent
{
public:
        DeviceStateEvent(_In_ usbip::device_state st) : 
                wxEvent(0, EVT_DEVICE_STATE),
                m_state(std::move(st)) {}

        wxEvent *Clone() const override { return new DeviceStateEvent(*this); }
        auto& get() const noexcept { return m_state; }

private:
        usbip::device_state m_state;
};
wxDEFINE_EVENT(EVT_DEVICE_STATE, DeviceStateEvent);


MainFrame::MainFrame(_In_ usbip::Handle read) : 
        Frame(nullptr),
        m_read(std::move(read))
{
        wxASSERT(m_read);

        if (m_iocp) {
                Bind(EVT_DEVICE_STATE, &MainFrame::on_device_state, this);
                m_thread = std::thread(&MainFrame::read_loop, this);
        }
}

MainFrame::~MainFrame() 
{
        if (m_iocp) {
                Unbind(EVT_DEVICE_STATE, &MainFrame::on_device_state, this);
        }
}

void MainFrame::join()
{
        wxASSERT(m_iocp);

        if (!PostQueuedCompletionStatus(m_iocp.get(), 0, CompletionKeyQuit, nullptr)) { // signal to quit read_loop()
                log_last_error("PostQueuedCompletionStatus"); 
                m_iocp.close(); // "dirty" way to quit
        } 

        try {
                m_thread.join();
        } catch (std::exception &e) {
                wxLogError("%s exception: %s", e.what(), __func__);
        }
}

void MainFrame::on_close(wxCloseEvent &event)
{
        if (m_thread.joinable()) {
                join();
        } 

        Frame::on_close(event);
}

void MainFrame::on_exit(wxCommandEvent&)
{
        Close(true);
}

void MainFrame::log_last_error(_In_ const char *what, _In_ DWORD msg_id)
{
        auto s = GetLastErrorMsg(msg_id);
        auto text = wxString::Format("%s: %s", what, s);
        SetStatusText(text);
}

DWORD MainFrame::read(_In_ void *buf, _In_ DWORD len)
{
        DWORD actual{};

        if (OVERLAPPED overlapped{}, *pov{}; !ReadFile(m_read.get(), buf, len, &actual, &overlapped)) {

                if (auto err = GetLastError(); err != ERROR_IO_PENDING) {
                        log_last_error("ReadFile", err);
                        wxASSERT(!actual);
                } else if (ULONG_PTR key{}; !GetQueuedCompletionStatus(m_iocp.get(), &actual, &key, &pov, INFINITE)) {
                        log_last_error("GetQueuedCompletionStatus");
                } else if (key == CompletionKeyQuit) {
                        wxASSERT(!actual);
                }
        }

        wxASSERT(actual <= len);
        return actual;
}

void MainFrame::read_loop()
{
        auto len = usbip::vhci::get_device_state_size();
        auto buf = std::make_unique_for_overwrite<char[]>(len);

        while (auto actual = read(buf.get(), len)) {

                if (usbip::device_state st; !vhci::get_device_state(st, buf.get(), actual)) {
                        log_last_error("vhci::get_device_state");
                        break;
                } else {
                        auto evt = new DeviceStateEvent(std::move(st));
                        QueueEvent(evt); // see on_device_state()
                }
        }
}

void MainFrame::on_device_state(_In_ DeviceStateEvent &event)
{
        auto &st = event.get();
        auto &loc = st.device.location;
        auto s = std::format("{}:{}/{} {}", loc.hostname, loc.service, loc.busid, vhci::get_state_str(st.state));
        SetStatusText(wxString::FromUTF8(s));
}

void MainFrame::on_list(wxCommandEvent&)
{
        m_treeCtrlList->DeleteAllItems();

        auto sock = connect("pc", usbip::get_tcp_port());
        if (!sock) {
                log_last_error("usbip::connect");
                return;
        }

        auto dev = [this] (auto /*idx*/, auto &dev)
        {
                auto busid = wxString::FromUTF8(dev.busid);
                m_treeCtrlList->AddRoot(busid);
        };

        auto intf = [this] (auto /*dev_idx*/, auto& /*dev*/, auto /*idx*/, auto& /*intf*/) {};

        if (!enum_exportable_devices(sock.get(), dev, intf)) {
                log_last_error("usbip::enum_exportable_devices");
        }
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
