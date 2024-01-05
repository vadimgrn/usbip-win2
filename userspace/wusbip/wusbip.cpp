/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include "utils.h"

#include <libusbip/remote.h>
#include <libusbip/vhci.h>

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

        if (auto read = init(err) ? vhci::open() : Handle()) {
                auto frame = new MainFrame(std::move(read));
                frame->Show(true);
                return true;
        }

        if (err.empty()) {
                err = GetLastErrorMsg();
        }

        wxSafeShowMessage(err, _("Fatal error"));
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
        set_log_level();
        Bind(EVT_DEVICE_STATE, &MainFrame::on_device_state, this);
}

MainFrame::~MainFrame() 
{
        Unbind(EVT_DEVICE_STATE, &MainFrame::on_device_state, this);
}

void MainFrame::on_close(wxCloseEvent &event)
{
        break_read_loop();
        m_read_thread.join();

        Frame::on_close(event);
}

void MainFrame::on_exit(wxCommandEvent&)
{
        Close(true);
}

void MainFrame::set_log_level()
{
        auto verbose = m_log->GetVerbose(); // --verbose option has passed
        if (!verbose) {
                m_log->SetVerbose(true); // produce messages for wxLOG_Info
        }

        auto lvl = verbose ? wxLOG_Info : wxLOG_Status;
        m_log->SetLogLevel(lvl);

        auto id = ID_LOG_LEVEL_ERROR + (lvl - wxLOG_Error);
        wxASSERT(id <= ID_LOG_LEVEL_INFO);

        auto item = m_menu_log->FindItem(id);
        wxASSERT(item);

        wxASSERT(item->IsRadio());
        item->Check(true);
}

void MainFrame::read_loop()
{
        auto on_exit = [] (auto frame)
        {
                std::lock_guard<std::mutex> lock(frame->m_read_close_mtx);
                frame->m_read.close();
        };

        std::unique_ptr<MainFrame, decltype(on_exit)> ptr(this, on_exit);

        for (usbip::device_state st; vhci::read_device_state(m_read.get(), st); ) {
                auto evt = new DeviceStateEvent(std::move(st));
                QueueEvent(evt); // see on_device_state()
        }

        if (auto err = GetLastError(); err != ERROR_OPERATION_ABORTED) { // see CancelSynchronousIo
                wxLogWarning("vhci::read_device_state error %lu: %s", err, GetLastErrorMsg(err));
        }
}

void MainFrame::break_read_loop()
{
        auto cancel_read = [this] // CancelSynchronousIo hangs if thread was terminated
        {
                std::lock_guard<std::mutex> lock(m_read_close_mtx);
                return !m_read || CancelSynchronousIo(m_read_thread.native_handle());
        };

        for (int i = 0; i < 300 && !cancel_read(); ++i, std::this_thread::sleep_for(std::chrono::milliseconds(100))) {
                if (auto err = GetLastError(); err != ERROR_NOT_FOUND) { // cannot find a request to cancel
                        wxLogSysError(err, "CancelSynchronousIo");
                        break;
                }
        }
}

void MainFrame::on_device_state(_In_ DeviceStateEvent &event)
{
        auto &st = event.get();
        auto &loc = st.device.location;
        auto s = std::format("{}:{}/{} {}", loc.hostname, loc.service, loc.busid, vhci::get_state_str(st.state));
        wxLogStatus(wxString::FromUTF8(s));
}

void MainFrame::on_log_show_update_ui(wxUpdateUIEvent &event)
{
        auto f = m_log->GetFrame();
        event.Check(f->IsVisible());
}

void MainFrame::on_log_show(wxCommandEvent &event)
{
        bool checked = event.GetInt();
        m_log->Show(checked);
}

void MainFrame::on_log_level(wxCommandEvent &event)
{
        auto lvl = static_cast<wxLogLevelValues>(wxLOG_Error + (event.GetId() - ID_LOG_LEVEL_ERROR));
        wxASSERT(lvl >= wxLOG_Error && lvl <= wxLOG_Info);

        m_log->SetLogLevel(lvl);
}

void MainFrame::on_list(wxCommandEvent&)
{
        m_treeCtrlList->DeleteAllItems();

        auto host = "pc";
        auto port = usbip::get_tcp_port();

        auto sock = connect(host, port);
        if (!sock) {
                auto err = GetLastError();
                wxLogError("connect %s:%s error %lu: %s", host, port, err, GetLastErrorMsg(err));
                return;
        }

        auto dev = [this] (auto /*idx*/, auto &dev)
        {
                auto busid = wxString::FromUTF8(dev.busid);
                m_treeCtrlList->AddRoot(busid);
        };

        auto intf = [this] (auto /*dev_idx*/, auto& /*dev*/, auto /*idx*/, auto& /*intf*/) {};

        if (!enum_exportable_devices(sock.get(), dev, intf)) {
                auto err = GetLastError();
                wxLogError("enum_exportable_devices error %lu: %s", err, GetLastErrorMsg(err));
        }
}

void MainFrame::on_attach(wxCommandEvent&)
{
        wxLogStatus(__func__);
}

void MainFrame::on_detach(wxCommandEvent&)
{
        wxLogStatus(__func__);
}

void MainFrame::on_port(wxCommandEvent&)
{
        m_treeCtrlList->DeleteAllItems();
        auto &vhci = get_vhci();

        bool ok{};
        auto devices = usbip::vhci::get_imported_devices(vhci.get(), ok);
        if (!ok) {
                auto err = GetLastError();
                wxLogError("get_imported_devices error %lu: %s", err, GetLastErrorMsg(err));
                return;
        }

        for (auto &i: devices) {
                auto &loc = i.location;
                auto s = std::format("{}:{}/{}", loc.hostname, loc.service, loc.busid);
                m_treeCtrlList->AddRoot(wxString::FromUTF8(s));        
        }
}
