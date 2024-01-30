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
#include <wx/busyinfo.h>
#include <wx/dataview.h>
#include <wx/config.h>

#include <wx/persist.h>
#include <wx/persist/toplevel.h>

#include <format>

namespace
{

using namespace usbip;

auto &g_key_devices = L"/devices";
auto &g_key_url = L"url";


class App : public wxApp
{
public:
        bool OnInit() override;

private:
        void set_names();
};

bool App::OnInit()
{
        if (wxApp::OnInit()) {
                set_names();
        } else {
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

        wxSafeShowMessage(_("Fatal error"), err);
        return false;
}

void App::set_names()
{
        auto &v = win::get_file_version();

        SetAppName(wx_string(v.GetProductName()));
        SetVendorName(wx_string(v.GetCompanyName()));
}

/*
 * port can be zero, speed can be UsbLowSpeed aka zero.
 */
auto is_filled(_In_ const imported_device &d) noexcept
{
        return d.devid && d.vendor && d.product;
}

inline auto is_empty(_In_ const imported_device &d) noexcept
{
        return !is_filled(d);
}

void log(_In_ const device_state &st)
{
        auto &d = st.device;
        auto &loc = d.location;

        auto s = std::format("{}:{}/{} {}, port {}, devid {:04x}, speed {}, vid {:02x}, pid {:02x}", 
                                loc.hostname, loc.service, loc.busid, vhci::get_state_str(st.state), 
                                d.port, d.devid, static_cast<int>(d.speed), d.vendor, d.product);

        wxLogVerbose(wxString::FromUTF8(s));
}

auto get_selections(_In_ wxTreeListCtrl &tree)
{
        wxTreeListItems v;
        tree.GetSelections(v);
        return v;
}

auto has_items(_In_ const wxTreeListCtrl &t) noexcept
{
        return t.GetFirstItem().IsOk();
}

} // namespace

wxIMPLEMENT_APP(App);

class DeviceStateEvent : public wxEvent
{
public:
        DeviceStateEvent(_In_ device_state st) : 
                wxEvent(0, EVT_DEVICE_STATE),
                m_state(std::move(st)) {}

        wxEvent *Clone() const override { return new DeviceStateEvent(*this); }
        auto& get() const noexcept { return m_state; }

private:
        device_state m_state;
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

MainFrame::MainFrame(_In_ Handle read) : 
        Frame(nullptr),
        m_read(std::move(read)),
        m_log(new LogWindow(this, m_menu_log->FindItem(ID_LOG_TOGGLE)))
{
        wxASSERT(m_read);
        Bind(EVT_DEVICE_STATE, &MainFrame::on_device_state, this);

        init();
        post_refresh();
}

MainFrame::~MainFrame() 
{
        Unbind(EVT_DEVICE_STATE, &MainFrame::on_device_state, this);
}

void MainFrame::init()
{
        if (auto cfg = wxConfig::Get()) {
                cfg->SetStyle(wxCONFIG_USE_LOCAL_FILE);
        }

        auto app_name = wxGetApp().GetAppDisplayName();
        SetTitle(app_name);

        set_log_level();
        m_textCtrlServer->SetMaxLength(NI_MAXHOST);

        auto port = get_tcp_port();
        m_spinCtrlPort->SetValue(wxString::FromAscii(port)); // NI_MAXSERV

        wxPersistentRegisterAndRestore(this, app_name);
}

void MainFrame::post_refresh()
{
        wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, wxID_REFRESH);
        wxPostEvent(m_menu_devices, evt);
}

void MainFrame::on_close(wxCloseEvent &event)
{
        wxLogVerbose(wxString::FromAscii(__func__));

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

        for (device_state st; vhci::read_device_state(m_read.get(), st); ) {
                auto evt = new DeviceStateEvent(std::move(st));
                QueueEvent(evt); // see on_device_state()
        }

        if (auto err = GetLastError(); err != ERROR_OPERATION_ABORTED) { // see CancelSynchronousIo
                wxLogError(_("vhci::read_device_state error %#lx\n%s"), err, GetLastErrorMsg(err));
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
                        wxLogError(L"CancelSynchronousIo error %lu\n%s", err, wxSysErrorMsg(err));
                        break; // wxLogSysError does not compile if wxNO_IMPLICIT_WXSTRING_ENCODING is set
                }
        }
}

void MainFrame::on_device_state(_In_ DeviceStateEvent &event)
{
        auto &tree = *m_treeListCtrl;

        auto &st = event.get();
        log(st);

        auto [dev, appended] = find_device(st.device.location, true);

        auto update = true;
        auto set_state = true;

        if (st.state == state::connecting) {
                auto state = tree.GetItemText(dev, col<ID_COL_STATE>()); // can be empty
                tree.SetItemText(dev, col<ID_COL_SAVED_STATE>(), state);
                wxLogVerbose(_("current state='%s' saved"), state);
        }

        if (!(st.state == state::disconnected && is_empty(st.device))) { // connection has failed/closed
                //
        } else if (auto saved_state = tree.GetItemText(dev, col<ID_COL_SAVED_STATE>()); saved_state.empty()) {
                wxLogVerbose(_("remove transient device"));
                remove_device(dev);
                appended = false;
                update = false;
        } else {
                tree.SetItemText(dev, col<ID_COL_STATE>(), saved_state);
                wxLogVerbose(_("saved state='%s' restored"), saved_state);
                set_state = false;
        }

        if (update) {
                update_device(dev, st, set_state);
        }

        if (!appended) {
                // as is
        } else if (auto server = tree.GetItemParent(dev); !tree.IsExpanded(server)) {
                tree.Expand(server);
        }
}

void MainFrame::on_has_items_update_ui(wxUpdateUIEvent &event)
{
        auto ok = has_items(*m_treeListCtrl);
        event.Enable(ok);
}

void MainFrame::on_has_selections_update_ui(wxUpdateUIEvent &event)
{
        auto v = get_selections(*m_treeListCtrl);
        event.Enable(!v.empty());
}

void MainFrame::on_select_all(wxCommandEvent&)
{
        m_treeListCtrl->SelectAll();
}

bool MainFrame::is_persistent(_In_ wxTreeListItem device)
{
        auto &tree = *m_treeListCtrl;
   
        wxASSERT(tree.GetItemParent(device).IsOk()); // server
        wxASSERT(!tree.GetFirstChild(device).IsOk());

        auto &s = tree.GetItemText(device, col<ID_COL_PERSISTENT>());
        return !s.empty();
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
        wxLogVerbose(wxString::FromAscii(__func__));

        auto lvl = static_cast<wxLogLevelValues>(wxLOG_Error + (event.GetId() - ID_LOGLEVEL_ERROR));
        wxASSERT(lvl >= wxLOG_Error && lvl <= wxLOG_Info);

        m_log->SetLogLevel(lvl);
}

bool MainFrame::attach(_In_ const wxString &url, _In_ const wxString &busid)
{
        wxString hostname;
        wxString service;

        if (!split_server_url(url, hostname, service)) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
        }

        device_location loc {
                .hostname = hostname.ToStdString(wxConvUTF8),
                .service = service.ToStdString(wxConvUTF8),
                .busid = busid.ToStdString(wxConvUTF8),
        };

        wxWindowDisabler dis;
        wxBusyInfo wait(wxString::Format(_("Attaching %s/%s"), url, busid), this);

        auto &vhci = get_vhci(); 
        return vhci::attach(vhci.get(), loc);
}

void MainFrame::on_attach(wxCommandEvent&)
{
        wxLogVerbose(wxString::FromAscii(__func__));
        
        for (auto &tree = *m_treeListCtrl; auto &item: get_selections(tree)) {

                auto parent = tree.GetItemParent(item);
                if (parent == tree.GetRootItem()) {
                        continue;
                }

                auto url = tree.GetItemText(parent); // server
                auto busid = tree.GetItemText(item); // column<ID_COL_BUSID>()

                if (!attach(url,  busid)) {
                        auto err = GetLastError();
                        wxLogError(_("Cannot attach %s/%s\nError %#lx\n%s"), url, busid, err, GetLastErrorMsg(err));
                }
        }
}

void MainFrame::on_detach(wxCommandEvent&)
{
        wxLogVerbose(wxString::FromAscii(__func__));
        
        for (auto &tree = *m_treeListCtrl; auto &item: get_selections(tree)) {

                auto parent = tree.GetItemParent(item);
                if (parent == tree.GetRootItem()) {
                        continue;
                }

                auto port = get_port(item);
                if (!port) {
                        continue;
                }

                if (auto &vhci = get_vhci(); !vhci::detach(vhci.get(),  port)) {
                        auto err = GetLastError();

                        auto url = tree.GetItemText(parent); // server
                        auto busid = tree.GetItemText(item); // column<ID_COL_BUSID>()

                        wxLogError(_("Cannot detach %s/%s\nError %#lx\n%s"), url, busid, err, GetLastErrorMsg(err));
                }
        }
}

void MainFrame::on_detach_all(wxCommandEvent&) 
{
        wxLogVerbose(wxString::FromAscii(__func__));

        if (auto &vhci = get_vhci(); !vhci::detach(vhci.get(), -1)) {
                auto err = GetLastError();
                wxLogError(_("Cannot detach all devices\nError %#lx\n%s"), err, GetLastErrorMsg(err));
        }

        post_refresh();
}

void MainFrame::on_refresh(wxCommandEvent&)
{
        wxLogVerbose(wxString::FromAscii(__func__));

        auto &tree = *m_treeListCtrl;
        tree.DeleteAllItems();

        bool ok{};
        auto devices = vhci::get_imported_devices(get_vhci().get(), ok);
        if (!ok) {
                auto err = GetLastError();
                wxLogError(_("get_imported_devices error %#lx\n%s"), err, GetLastErrorMsg(err));
                return;
        }

        for (auto &dev: devices) {
                auto [item, appended] = find_device(dev.location, true);
                wxASSERT(appended);

                update_device(item, dev, state::plugged, true);

                if (auto server = tree.GetItemParent(item); !tree.IsExpanded(server)) {
                        tree.Expand(server);
                }
        }
}

wxTreeListItem MainFrame::find_server(_In_ const wxString &url, _In_ bool append)
{
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

std::pair<wxTreeListItem, bool> MainFrame::find_device(
        _In_ const wxString &url, _In_ const wxString &busid, _In_ bool append)
{
        std::pair<wxTreeListItem, bool> res;

        auto server = find_server(url, append);
        if (!server.IsOk()) {
                return res;
        }

        auto &tree = *m_treeListCtrl;

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

std::pair<wxTreeListItem, bool> MainFrame::find_device(_In_ const device_location &loc, _In_ bool append)
{
        auto url = make_server_url(loc);
        auto busid = wxString::FromUTF8(loc.busid);

        return find_device(url, busid, append);
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

void MainFrame::update_device(
        _In_ wxTreeListItem dev, _In_ const device_columns &dc, _In_ unsigned int flags)
{
        auto &tree = *m_treeListCtrl;

        wxASSERT(dev.IsOk());
        wxASSERT(tree.GetItemText(dev) == dc[col<ID_COL_BUSID>()]);
        wxASSERT(tree.GetItemText(tree.GetItemParent(dev)) == get_url(dc));

        if (flags & SET_OTHERS) {
                for (auto col: { col<ID_COL_PORT>(), col<ID_COL_SPEED>(), col<ID_COL_VENDOR>(), 
                                 col<ID_COL_PRODUCT>(), col<ID_COL_COMMENTS>() }) {
                        tree.SetItemText(dev, col, dc[col]);
                }
        }

        if (flags & SET_STATE) {
                constexpr auto idx = col<ID_COL_STATE>();
                tree.SetItemText(dev, idx, dc[idx]);
        }

        if (flags & SET_LOADED) {
                tree.SetItemText(dev, col<ID_COL_LOADED>(), L"Yes");
        }
}

void MainFrame::update_device(_In_ wxTreeListItem device, _In_ const device_state &st, _In_ bool set_state)
{
        auto &dev = st.device;

        device_columns dc;
        get_url(dc) = make_server_url(dev.location);
        dc[col<ID_COL_BUSID>()] = wxString::FromUTF8(dev.location.busid);

        unsigned int flags{};

        if (set_state) {
                flags |= SET_STATE;
                dc[col<ID_COL_STATE>()] = _(vhci::get_state_str(st.state));
        }

        if (is_empty(st.device)) {
                update_device(device, dc, flags);
                return;
        }

        if (dev.port && st.state != state::unplugged) {
                dc[col<ID_COL_PORT>()] = wxString::Format(L"%02d", dev.port); // XX for proper sorting
        }

        dc[col<ID_COL_SPEED>()] = get_speed_str(dev.speed);

        auto str = [] (auto id, auto sv)
        {
                return sv.empty() ? wxString::Format(L"%04x", id) : wxString::FromAscii(sv.data(), sv.size());
        };

        auto [vendor, product] = get_ids().find_product(dev.vendor, dev.product);

        dc[col<ID_COL_VENDOR>()] = str(dev.vendor, vendor);
        dc[col<ID_COL_PRODUCT>()] = str(dev.product, product);

        update_device(device, dc, flags | SET_OTHERS);
}

void MainFrame::update_device(
        _In_ wxTreeListItem device, _In_ const imported_device &dev, _In_ state state, _In_ bool set_state)
{
        device_state st{ .device = dev, .state = state };
        update_device(device, st, set_state);
}

void MainFrame::on_help_about(wxCommandEvent&)
{
        using usbip::wx_string;
        auto &v = win::get_file_version();
 
        wxAboutDialogInfo d;

        d.SetVersion(wx_string(v.GetProductVersion()));
        d.SetDescription(wx_string(v.GetFileDescription()));
        d.SetCopyright(wx_string(v.GetLegalCopyright()));

        d.AddDeveloper(L"Vadym Hrynchyshyn\t<vadimgrn@gmail.com>");
        d.SetWebSite(L"https://github.com/vadimgrn/usbip-win2", _("GitHub project page"));

        d.SetLicence(_("GNU General Public License v3.0"));
        //d.SetIcon();

        wxAboutBox(d, this);
}

void MainFrame::add_exported_devices(wxCommandEvent&)
{
        auto host = m_textCtrlServer->GetValue();
        auto port = wxString::Format(L"%d", m_spinCtrlPort->GetValue());

        wxLogVerbose(L"%s, host='%s', port='%s'", wxString::FromAscii(__func__), host, port);

        auto u8_host = host.ToStdString(wxConvUTF8);
        auto u8_port = port.ToStdString(wxConvUTF8);

        Socket sock;
        {
                wxWindowDisabler dis;
                wxBusyInfo wait(wxString::Format(_("Connecting to %s:%s"), host, port), this);

                sock = connect(u8_host.c_str(), u8_port.c_str());
        }

        if (!sock) {
                auto err = GetLastError();
                wxLogError(_("Cannot connect to %s:%s\nError %#lx\n%s"), host, port, err, GetLastErrorMsg(err));
                return;
        }

        auto dev = [this, host = std::move(u8_host), port = std::move(u8_port)] (auto, auto &device)
        {
                auto dev = make_imported_device(std::move(host), std::move(port), device);

                auto [item, appended] = find_device(dev.location, true);
                update_device(item, dev, state::unplugged, appended);
        };

        auto intf = [this] (auto /*dev_idx*/, auto& /*dev*/, auto /*idx*/, auto& /*intf*/) {};

        if (!enum_exportable_devices(sock.get(), dev, intf)) {
                auto err = GetLastError();
                wxLogError(_("enum_exportable_devices error %#lx\n%s"), err, GetLastErrorMsg(err));
        }

        auto url = make_server_url(host, port);
        auto server = find_server(url, false);
        
        if (auto &tree = *m_treeListCtrl; server.IsOk() && !tree.IsExpanded(server)) {
                tree.Expand(server);
        }
}

wxDataViewColumn& MainFrame::get_column(_In_ int col_id) const noexcept
{
        auto &view = *m_treeListCtrl->GetDataView();

        auto pos = col(col_id);
        wxASSERT(pos < view.GetColumnCount());
        
        auto col = view.GetColumn(pos);
        wxASSERT(col);

        return *col;
}

void MainFrame::on_view_column_update_ui(wxUpdateUIEvent &event)
{
        auto col_id = event.GetId();
        auto &col = get_column(col_id);

        event.Check(col.IsShown());
}

void MainFrame::on_view_column(wxCommandEvent &event)
{
        auto col_id = event.GetId();
        auto &col = get_column(col_id);

        bool checked = event.GetInt();
        col.SetHidden(!checked);
}

void MainFrame::on_item_activated(wxTreeListEvent &event)
{
        auto &tree = *m_treeListCtrl;
        
        if (auto item = event.GetItem(); tree.GetItemParent(item) == tree.GetRootItem()) {
                // item is a server
        } else if (auto state = tree.GetItemText(item, col<ID_COL_STATE>());
                   state == _(vhci::get_state_str(state::unplugged))) {
                on_attach(event);
        } else if (state == _(vhci::get_state_str(state::plugged))) {
                on_detach(event);
        }
}

void MainFrame::on_view_labels_update_ui(wxUpdateUIEvent &event)
{
        auto shown = m_auiToolBar->HasFlag(wxAUI_TB_TEXT);
        event.Check(shown);
}

void MainFrame::on_view_labels(wxCommandEvent &)
{
        auto &tb = *m_auiToolBar;
        tb.ToggleWindowStyle(wxAUI_TB_TEXT);
        tb.Refresh();
}

int MainFrame::get_port(_In_ wxTreeListItem dev) const
{
        auto &tree = *m_treeListCtrl;

        wxASSERT(!tree.GetFirstChild(dev).IsOk());
        auto str = tree.GetItemText(dev, col<ID_COL_PORT>());

        int port;
        return str.ToInt(&port) ? port : 0;
}

auto& MainFrame::get_keys()
{
        struct {
                const wchar_t *key;
                unsigned int col;
        } static const v[] = {
                { L"busid", col<ID_COL_BUSID>() },
                { L"speed", col<ID_COL_SPEED>() },
                { L"vendor", col<ID_COL_VENDOR>() },
                { L"product", col<ID_COL_PRODUCT>() },
                { L"comments", col<ID_COL_COMMENTS>() },
        };

        return v;
}

void MainFrame::on_save(wxCommandEvent&)
{
        auto &cfg = *wxConfig::Get();
        auto path = cfg.GetPath();

        cfg.DeleteGroup(g_key_devices);
        cfg.SetPath(g_key_devices);

        int cnt = 0;
        auto &tree = *m_treeListCtrl;
 
        for (auto &item: get_selections(tree)) {

                auto parent = tree.GetItemParent(item);
                if (parent == tree.GetRootItem()) { // server
                        continue;
                }

                cfg.SetPath(wxString::Format(L"%d", ++cnt));
                cfg.Write(g_key_url, tree.GetItemText(parent)); // server

                for (auto [key, col] : get_keys()) {
                        auto value = tree.GetItemText(item, col);
                        cfg.Write(key, value);
                }

                cfg.SetPath(L"..");
        }

        cfg.Flush();
        cfg.SetPath(path);

        SetStatusText(wxString::Format(_("%d device(s) saved"), cnt));

        if (!cnt && has_items(tree)) {
                wxMessageBox(_("No selections were made"), _("Nothing to save"), wxICON_WARNING);
        }
}

void MainFrame::on_load(wxCommandEvent&) 
{
        auto &tree = *m_treeListCtrl;
        tree.DeleteAllItems();

        auto &cfg = *wxConfig::Get();

        auto path = cfg.GetPath();
        cfg.SetPath(g_key_devices);

        int cnt = 0;
        wxString name;
        long idx;

        for (auto ok = cfg.GetFirstGroup(name, idx); ok; cfg.SetPath(L".."), ok = cfg.GetNextGroup(name, idx)) {

                cfg.SetPath(name);

                device_columns dev;
                auto &url = get_url(dev);

                url = cfg.Read(g_key_url);
                if (url.empty()) {
                        continue;
                }

                dev[col<ID_COL_STATE>()] = _(vhci::get_state_str(state::unplugged));

                for (auto [key, col] : get_keys()) {
                        dev[col] = cfg.Read(key);
                }

                if (dev[col<ID_COL_BUSID>()].empty()) {
                        continue;
                }

                auto [device, appended] = find_device(url, dev[col<ID_COL_BUSID>()], true);
                update_device(device, dev, SET_STATE | SET_LOADED | SET_OTHERS);
                cnt += appended;

                if (auto server = tree.GetItemParent(device); !tree.IsExpanded(server)) {
                        tree.Expand(server);
                }
        }

        cfg.SetPath(path);
        SetStatusText(wxString::Format(_("%d device(s) loaded"), cnt));
}
