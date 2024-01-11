/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include "utils.h"

#include <libusbip/remote.h>
#include <libusbip/src/usb_ids.h>
#include <libusbip/src/file_ver.h>

#include <wx/log.h>
#include <wx/app.h>
#include <wx/event.h>
#include <wx/msgdlg.h>
#include <wx/aboutdlg.h>

#include <format>

namespace
{

using namespace usbip;

class App : public wxApp
{
public:
        bool OnInit() override;

private:
        void set_names();
};

bool App::OnInit()
{
        if (!wxApp::OnInit()) {
                return false;
        }

        wxString err;

        if (auto read = init(err) ? vhci::open() : Handle()) {
                set_names(); // after init()
                auto frame = new MainFrame(std::move(read));
                frame->SetTitle(GetAppDisplayName());
                frame->Show(true);
                return true;
        }

        if (err.empty()) {
                err = GetLastErrorMsg();
        }

        wxSafeShowMessage(err, _("Fatal error"));
        return false;
}

void App::set_names()
{
        auto &v = win::get_file_version();

        SetAppName(wx_string(v.GetProductName()));
        SetVendorName(wx_string(v.GetCompanyName()));
}

auto make_server_url(_In_ const device_location &loc)
{
        auto s = std::format("{}:{}", loc.hostname, loc.service);
        return wxString::FromUTF8(s);
}

auto is_filled(_In_ const imported_device &d) noexcept
{
        return d.port > 0 && d.devid && d.vendor && d.product;
}

inline auto is_empty(_In_ const imported_device &d) noexcept
{
        return !is_filled(d);
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

LogWindow::LogWindow(_In_ wxWindow *parent, _In_ const wxMenuItem *log_toggle) : 
        wxLogWindow(parent, _("Log records"), false)
{
        wxASSERT(log_toggle);

        auto acc = log_toggle->GetAccel();
        wxASSERT(acc);

        wxAcceleratorEntry entry(acc->GetFlags(), acc->GetKeyCode(), wxID_CLOSE);
        wxAcceleratorTable table(1, &entry);

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


MainFrame::MainFrame(_In_ usbip::Handle read) : 
        Frame(nullptr),
        m_read(std::move(read)),
        m_log(new LogWindow(this, m_menu_log->FindItem(ID_LOG_TOGGLE)))
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
        wxLogVerbose(__func__);

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

        auto id = ID_LOGLEVEL_ERROR + (lvl - wxLOG_Error);
        wxASSERT(id <= ID_LOGLEVEL_INFO);

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
        
        {
                auto state = vhci::get_state_str(st.state);
                auto s = std::format("{}:{}/{} {}, port {}", loc.hostname, loc.service, loc.busid, state, st.device.port);
                wxLogVerbose(wxString::FromUTF8(s));
        }

        auto [dev, appended] = find_device(loc, true);
        update_device(dev, st);

        if (st.state == usbip::state::disconnected && is_empty(st.device)) {
                remove_device(dev);
        } else if (auto &tree = *m_treeListCtrl; !appended) {
                // as is
        } else if (auto server = tree.GetItemParent(dev); !tree.IsExpanded(server)) {
                tree.Expand(server);
        }
}

void MainFrame::on_log_show_update_ui(wxUpdateUIEvent &event)
{
        auto f = m_log->GetFrame();
        event.Check(f->IsVisible());
}

void MainFrame::on_log_show(wxCommandEvent &event)
{
        wxLogVerbose(__func__);

        bool checked = event.GetInt();
        m_log->Show(checked);
}

void MainFrame::on_log_level(wxCommandEvent &event)
{
        wxLogVerbose(__func__);

        auto lvl = static_cast<wxLogLevelValues>(wxLOG_Error + (event.GetId() - ID_LOGLEVEL_ERROR));
        wxASSERT(lvl >= wxLOG_Error && lvl <= wxLOG_Info);

        m_log->SetLogLevel(lvl);
}

void MainFrame::on_list(wxCommandEvent&)
{
        wxLogVerbose(__func__);
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
        wxLogVerbose(__func__);
}

void MainFrame::on_detach(wxCommandEvent&)
{
        wxLogVerbose(__func__);
}

void MainFrame::on_refresh(wxCommandEvent&)
{
        wxLogVerbose(__func__);

        auto &tree = *m_treeListCtrl;
        tree.DeleteAllItems();

        bool ok{};
        auto devices = usbip::vhci::get_imported_devices(get_vhci().get(), ok);
        if (!ok) {
                auto err = GetLastError();
                wxLogError("get_imported_devices error %lu: %s", err, GetLastErrorMsg(err));
                return;
        }

        for (auto &dev: devices) {
                auto [item, appended] = find_device(dev.location, true);
                wxASSERT(appended);

                update_device(item, dev);

                if (auto server = tree.GetItemParent(item); !tree.IsExpanded(server)) {
                        tree.Expand(server);
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

std::pair<wxTreeListItem, bool> MainFrame::find_device(_In_ const usbip::device_location &loc, _In_ bool append)
{
        std::pair<wxTreeListItem, bool> res;

        auto server = find_server(loc, append);
        if (!server.IsOk()) {
                return res;
        }

        auto &tree = *m_treeListCtrl;
        auto busid = wxString::FromUTF8(loc.busid);

        for (auto item = tree.GetFirstChild(server); item.IsOk(); item = tree.GetNextSibling(item)) {
                if (tree.GetItemText(item) == busid) {
                        return res = std::make_pair(item, false);
                }
        }

        if (append) {
                auto item = tree.AppendItem(server, busid);
                res = std::make_pair(item, true);
        }

        return res;
}

void MainFrame::remove_device(_In_ wxTreeListItem device)
{
        wxASSERT(device.IsOk());
        auto &tree = *m_treeListCtrl;

        auto server = tree.GetItemParent(device);
        tree.DeleteItem(device);

        if (auto child = tree.GetFirstChild(server); !child.IsOk()) { // has no children
                tree.DeleteItem(server);
        }
}

void MainFrame::update_device(_In_ wxTreeListItem device, _In_ const usbip::device_state &st)
{
        auto &dev = st.device;
        auto &tree = *m_treeListCtrl;

        wxASSERT(device.IsOk());
        wxASSERT(tree.GetItemText(device) == wxString::FromUTF8(dev.location.busid)); // COL_BUSID
        wxASSERT(tree.GetItemText(tree.GetItemParent(device)) == make_server_url(dev.location));

        auto str = [] (auto id, auto sv)
        {
                return sv.empty() ? wxString::Format("%04x", id) : wxString::FromAscii(sv.data(), sv.size());
        };

        tree.SetItemText(device, COL_PORT, wxString::Format("%02d", dev.port)); // XX for proper sorting
        tree.SetItemText(device, COL_SPEED, usbip::get_speed_str(dev.speed));

        auto [vendor, product] = usbip::get_ids().find_product(dev.vendor, dev.product);
        tree.SetItemText(device, COL_VID, str(dev.vendor, vendor));
        tree.SetItemText(device, COL_PID, str(dev.product, product));

        auto state_str = vhci::get_state_str(st.state);
        tree.SetItemText(device, COL_STATE, _(wxString::FromAscii(state_str)));
}

void MainFrame::update_device(_In_ wxTreeListItem device, _In_ const usbip::imported_device &d)
{
        usbip::device_state st{ .device = d, .state = usbip::state::plugged };
        update_device(device, st);
}

void MainFrame::on_help_about(wxCommandEvent&)
{
        using usbip::wx_string;
        auto &v = win::get_file_version();
 
        wxAboutDialogInfo d;

        d.SetVersion(wx_string(v.GetProductVersion()));
        d.SetDescription(wx_string(v.GetFileDescription()));
        d.SetCopyright(wx_string(v.GetLegalCopyright()));

        d.AddDeveloper(wxString::FromAscii("Vadym Hrynchyshyn\t<vadimgrn@gmail.com>"));

        d.SetWebSite(wxString::FromAscii("https://github.com/vadimgrn/usbip-win2"), 
                     _(wxString::FromAscii("GitHub project page")));

        d.SetLicence(wxString::FromAscii("GNU General Public License v3.0"));
        //d.SetIcon();

        wxAboutBox(d, this);
}
