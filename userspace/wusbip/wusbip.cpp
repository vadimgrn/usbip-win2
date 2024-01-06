/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include "utils.h"

#include <libusbip/remote.h>
#include <libusbip/vhci.h>
#include <libusbip/src/usb_ids.h>

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

auto make_server_url(_In_ const usbip::device_location &loc)
{
        auto s = std::format("{}:{}", loc.hostname, loc.service);
        return wxString::FromUTF8(s);
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

        wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, ID_CMD_REFRESH);
        wxPostEvent(m_menu_commands, evt);
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
/*
        m_treeCtrlList->DeleteAllItems();

        auto host = "pc";
        auto port = usbip::get_tcp_port();

        auto sock = connect(host, port);
        if (!sock) {
                auto err = GetLastError();
                wxLogError("connect %s:%s error %lu: %s", host, port, err, GetLastErrorMsg(err));
                return;
        }

        auto dev = [this] (auto, auto &dev)
        {
                auto busid = wxString::FromUTF8(dev.busid);
                m_treeCtrlList->AddRoot(busid);
        };
*/
        //auto intf = [this] (auto /*dev_idx*/, auto& /*dev*/, auto /*idx*/, auto& /*intf*/) {};
/*
        if (!enum_exportable_devices(sock.get(), dev, intf)) {
                auto err = GetLastError();
                wxLogError("enum_exportable_devices error %lu: %s", err, GetLastErrorMsg(err));
        }
*/
}

void MainFrame::on_attach(wxCommandEvent&)
{
        wxLogStatus(__func__);
}

void MainFrame::on_detach(wxCommandEvent&)
{
        wxLogStatus(__func__);
}

void MainFrame::on_refresh(wxCommandEvent&)
{
        auto &tree = *m_treeListCtrl;
        tree.DeleteAllItems();

        bool ok{};
        auto devices = usbip::vhci::get_imported_devices(get_vhci().get(), ok);
        if (!ok) {
                auto err = GetLastError();
                wxLogError("get_imported_devices error %lu: %s", err, GetLastErrorMsg(err));
                return;
        }

        auto &ids = usbip::get_ids();

        auto st = vhci::get_state_str(usbip::state::connected);
        auto connected = _(wxString::FromAscii(st));

        auto str = [] (auto id, auto sv)
        {
                return sv.empty() ? wxString::Format("%04x", id) : wxString::FromAscii(sv.data(), sv.size());
        };

        for (auto &d: devices) {
                auto srv = find_server(d.location, true);

                static_assert(!COL_BUSID);
                auto dev = tree.AppendItem(srv, wxString::FromUTF8(d.location.busid));

                tree.SetItemText(dev, COL_PORT, wxString::Format("%02d", d.port)); // XX for proper sorting
                tree.SetItemText(dev, COL_SPEED, usbip::get_speed_str(d.speed));

                auto [vendor, product] = ids.find_product(d.vendor, d.product);
                tree.SetItemText(dev, COL_VID, str(d.vendor, vendor));
                tree.SetItemText(dev, COL_PID, str(d.product, product));

                tree.SetItemText(dev, COL_STATE, connected);
                
                if (!tree.IsExpanded(srv)) {
                        tree.Expand(srv);
                }
        }
}

wxTreeListItem MainFrame::find_server(_In_ const usbip::device_location &loc, _In_ bool append)
{
        auto url = make_server_url(loc);

        auto &tree = *m_treeListCtrl;
        wxTreeListItem server;

        for (auto item = tree.GetFirstItem(); item.IsOk(); item = tree.GetNextSibling(item)) {
                if (tree.GetItemText(item) == url) {
                        return server = item;
                }
        }

        if (append) {
                server = tree.AppendItem(tree.GetRootItem(), url);
        }

        return server;
}