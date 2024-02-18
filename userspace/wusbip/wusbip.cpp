/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wusbip.h"
#include "utils.h"

#include <libusbip/remote.h>
#include <libusbip/persistent.h>
#include <libusbip/src/file_ver.h>

#include <wx/log.h>
#include <wx/app.h>
#include <wx/event.h>
#include <wx/msgdlg.h>
#include <wx/aboutdlg.h>
#include <wx/busyinfo.h>
#include <wx/dataview.h>
#include <wx/config.h>
#include <wx/textdlg.h>

#include <wx/persist.h>
#include <wx/persist/dataview.h>
#include <wx/persist/toplevel.h>

#include <format>
#include <set>

namespace
{

using namespace usbip;

auto &g_key_devices = L"/devices";
auto &g_key_url = L"url";
auto &g_persistent_mark = L"\u2713"; // CHECK MARK, 2714 HEAVY CHECK MARK


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

consteval auto get_saved_keys()
{
        using key_val = std::pair<const wchar_t* const, column_pos_t>;

        return std::to_array<key_val>({
                std::make_pair(L"busid", COL_BUSID),
                { L"speed", COL_SPEED },
                { L"vendor", COL_VENDOR },
                { L"product", COL_PRODUCT },
                { L"notes", COL_NOTES },
                });
}

consteval auto get_saved_flags()
{
        unsigned int flags{};

        for (auto [key, col]: get_saved_keys()) {
                flags |= mkflag(col);
        }

        return flags;
}

/*
 * For device_state.state 
 */
constexpr auto is_port_residual(_In_ state st)
{
        switch (st) {
        using enum state;
        case unplugged: // port > 0 if previous state was unplugging
        case connecting: // port is zero
        case connected: // port is zero
                return true;
        case plugged: // port > 0
        case disconnected: // port > 0 if previous state was plugged
        case unplugging: // port > 0
        default:
                return false;
        }
}

inline auto equal(_In_ const device_columns &a, _In_ const device_columns &b, _In_ column_pos_t pos)
{
        wxASSERT(pos < std::tuple_size_v<device_columns>);
        return a[pos] == b[pos];
}

auto as_set(_In_ std::vector<device_columns> v)
{
        return std::set<device_columns>(
                std::make_move_iterator(v.begin()), 
                std::make_move_iterator(v.end()));
}

/*
 * port can be zero, speed can be UsbLowSpeed aka zero.
 */
auto is_filled(_In_ const imported_device &d) noexcept
{
        return d.vendor && d.product; // d.devid
}

/*
 * @see is_empty(const device_columns&) 
 */
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

auto set_persistent_notes(
        _Inout_ device_columns &dc, _In_ unsigned int flags,
        _In_ const std::set<device_location> &persistent, 
        _In_opt_ const std::set<device_columns> *saved = nullptr)
{
        if (auto loc = make_device_location(dc); persistent.contains(loc)) {
                dc[COL_PERSISTENT] = g_persistent_mark;

                constexpr auto pers_flag = mkflag(COL_PERSISTENT);
                wxASSERT(!(flags & pers_flag));
                flags |= pers_flag;
        }

        if (!saved) {
                //
        } else if (auto i = saved->find(dc); i != saved->end()) {
                dc[COL_NOTES] = (*i)[COL_NOTES];

                constexpr auto notes_flag = mkflag(COL_NOTES);
                wxASSERT(!(flags & notes_flag));
                flags |= notes_flag;
        }

        return flags;
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

auto make_device_location(_In_ wxTreeListCtrl &tree, _In_ wxTreeListItem server, _In_ wxTreeListItem device)
{
        auto &url = tree.GetItemText(server);
        auto &busid = tree.GetItemText(device);

        return usbip::make_device_location(url, busid);
}

auto get_persistent()
{
        std::set<device_location> result;
        bool success{};
        
        if (auto lst = vhci::get_persistent(get_vhci().get(), success); !success) {
                auto err = GetLastError();
                wxLogError(_("Cannot load persistent info\nError %#lx\n%s"), err, GetLastErrorMsg(err));
        } else for (auto &loc: lst) {
                if (auto [i, inserted] = result.insert(std::move(loc)); !inserted) {
                        wxLogVerbose(_("%s: failed to insert %s:%s/%s"), 
                                        wxString::FromAscii(__func__), wxString::FromUTF8(i->hostname), 
                                        wxString::FromUTF8(i->service), wxString::FromUTF8(i->busid));
                }
        }

        return result;
}

auto get_saved()
{
        auto &cfg = *wxConfig::Get();

        auto path = cfg.GetPath();
        cfg.SetPath(g_key_devices);

        std::vector<device_columns> result;
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

                for (auto [key, col] : get_saved_keys()) {
                        dev[col] = cfg.Read(key);
                }

                if (dev[COL_BUSID].empty()) {
                        continue;
                }

                result.push_back(std::move(dev));
        }

        cfg.SetPath(path);
        return result;
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
        auto& get() noexcept { return m_state; }

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

MainFrame::MainFrame(_In_ Handle read) : 
        Frame(nullptr),
        m_read(std::move(read)),
        m_log(new LogWindow(this, m_menu_log->FindItem(ID_LOG_TOGGLE)))
{
        wxASSERT(m_read);
        Bind(EVT_DEVICE_STATE, &MainFrame::on_device_state, this);

        init();
        restore_state();
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
        set_menu_columns_labels();

        m_textCtrlServer->SetMaxLength(NI_MAXHOST);

        auto port = get_tcp_port();
        m_spinCtrlPort->SetValue(wxString::FromAscii(port)); // NI_MAXSERV

}

void MainFrame::restore_state()
{
        wxPersistentRegisterAndRestore(m_treeListCtrl->GetDataView(), m_treeListCtrl->GetName());
        wxPersistentRegisterAndRestore(this, L"MainFrame");
}

void MainFrame::post_refresh()
{
        wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, wxID_REFRESH);
        wxASSERT(m_menu_view->FindItem(wxID_REFRESH)); // command belongs to this menu
        wxPostEvent(m_menu_view, evt);
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

        auto [dev, added] = find_or_add_device(st.device.location);

        auto update = true;
        auto set_state = true;

        if (st.state == state::connecting) {
                auto state = tree.GetItemText(dev, COL_STATE); // can be empty
                tree.SetItemText(dev, COL_SAVED_STATE, state);
                wxLogVerbose(_("current state='%s' saved"), state);
        }

        if (!(st.state == state::disconnected && is_empty(st.device))) { // connection has failed/closed
                //
        } else if (auto saved_state = tree.GetItemText(dev, COL_SAVED_STATE); saved_state.empty()) {
                wxLogVerbose(_("remove transient device"));
                remove_device(dev);
                added = false;
                update = false;
        } else {
                tree.SetItemText(dev, COL_STATE, saved_state);
                wxLogVerbose(_("saved state='%s' restored"), saved_state);
                set_state = false;
        }

        if (update) {
                if (is_port_residual(st.state)) {
                        st.device.port = 0;
                }

                auto [dc, flags] = make_device_columns(st);

                if (added) {
                        auto persistent = get_persistent();
                        
                        auto saved = as_set(get_saved());
                        static_assert(get_saved_flags() & mkflag(COL_NOTES));

                        flags = set_persistent_notes(dc, flags, persistent, &saved);
                }

                update_device(dev, dc, flags);
        }

        if (!added) {
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

wxTreeListItem MainFrame::get_edit_notes_device()
{
        auto &tree = *m_treeListCtrl;
        wxTreeListItem item;

        if (auto v = get_selections(tree);
            v.size() == 1 && tree.GetItemParent(v.front()) != tree.GetRootItem()) { // server
                item = v.front();
        }

        return item;
}

void MainFrame::on_edit_notes_update_ui(wxUpdateUIEvent &event)
{
        auto item = get_edit_notes_device();
        event.Enable(item.IsOk());
}

void MainFrame::on_edit_notes(wxCommandEvent&)
{
        auto dev = get_edit_notes_device();
        if (!dev.IsOk()) {
                return;
        }

        auto &tree = *m_treeListCtrl;
        auto server = tree.GetItemParent(dev);

        auto url = tree.GetItemText(server);
        auto busid = tree.GetItemText(dev);
        auto caption = wxString::Format(_("Notes for %s/%s"), url, busid);

        auto vendor = tree.GetItemText(dev, COL_VENDOR);
        auto product = tree.GetItemText(dev, COL_PRODUCT);
        auto message = wxString::Format(L"%s\n%s", vendor, product);

        auto notes = tree.GetItemText(dev, COL_NOTES);

        wxTextEntryDialog dlg(this, message, caption, notes, wxTextEntryDialogStyle);
        dlg.SetMaxLength(256);

        if (dlg.ShowModal() == wxID_OK) {
                notes = dlg.GetValue();
                tree.SetItemText(dev, COL_NOTES, notes);
        }
}

bool MainFrame::is_persistent(_In_ wxTreeListItem device)
{
        auto &tree = *m_treeListCtrl;
   
        wxASSERT(tree.GetItemParent(device).IsOk()); // server
        wxASSERT(!tree.GetFirstChild(device).IsOk());

        auto &s = tree.GetItemText(device, COL_PERSISTENT);
        return !s.empty();
}

void MainFrame::set_persistent(_In_ wxTreeListItem device, _In_ bool persistent)
{
        auto &tree = *m_treeListCtrl;

        wxASSERT(tree.GetItemParent(device).IsOk()); // server
        wxASSERT(!tree.GetFirstChild(device).IsOk());

        wxString val;
        if (persistent) {
                val = g_persistent_mark; // CHECK MARK, 2714 HEAVY CHECK MARK
        }

        tree.SetItemText(device, COL_PERSISTENT, val);
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
                auto busid = tree.GetItemText(item);

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
                        auto busid = tree.GetItemText(item);

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

std::pair<wxTreeListItem, bool> MainFrame::find_or_add_device(_In_ const wxString &url, _In_ const wxString &busid)
{
        std::pair<wxTreeListItem, bool> res;

        auto &tree = *m_treeListCtrl;
        auto server = find_server(url, true);

        for (auto item = tree.GetFirstChild(server); item.IsOk(); item = tree.GetNextSibling(item)) {
                if (tree.GetItemText(item) == busid) {
                        return res = std::make_pair(item, false);
                }
        }

        auto item = tree.AppendItem(server, busid);
        return res = std::make_pair(item, true);
}

std::pair<wxTreeListItem, bool> MainFrame::find_or_add_device(_In_ const device_location &loc)
{
        auto url = make_server_url(loc);
        auto busid = wxString::FromUTF8(loc.busid);

        return find_or_add_device(url, busid);
}

std::pair<wxTreeListItem, bool> MainFrame::find_or_add_device(_In_ const device_columns &dc)
{
        auto &url = get_url(dc);
        return find_or_add_device(url, dc[COL_BUSID]);
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

void MainFrame::update_device(_In_ wxTreeListItem device, _In_ const device_columns &dc, _In_ unsigned int flags)
{
        auto &tree = *m_treeListCtrl;

        wxASSERT(device.IsOk());
        wxASSERT(tree.GetItemText(device) == dc[COL_BUSID]);
        wxASSERT(tree.GetItemText(tree.GetItemParent(device)) == get_url(dc));

        if (!flags) {
                return;
        }
        
        for (auto col: { COL_PORT, COL_SPEED, COL_VENDOR, COL_PRODUCT, COL_STATE, COL_PERSISTENT, COL_NOTES }) {
                if (auto &new_val = dc[col]; 
                    (flags & mkflag(col)) && new_val != tree.GetItemText(device, col)) {
                        tree.SetItemText(device, col, new_val);
                }
        }
}

void MainFrame::on_help_about(wxCommandEvent&)
{
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

        auto persistent = get_persistent();
        auto saved = as_set(get_saved());

        auto dev = [this, host = std::move(u8_host), port = std::move(u8_port), &persistent, &saved] (auto, auto &device)
        {
                device_state st {
                        .device = make_imported_device(std::move(host), std::move(port), device),
                        .state = state::unplugged
                };

                auto [dc, flags] = make_device_columns(st);

                static_assert(get_saved_flags() & mkflag(COL_NOTES));
                flags = set_persistent_notes(dc, flags, persistent, &saved);

                auto [item, added] = find_or_add_device(dc);
                if (!added) {
                        flags &= ~mkflag(COL_STATE); // clear
                }

                update_device(item, dc, flags);
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

void MainFrame::set_menu_columns_labels()
{
        constexpr auto cnt = COL_LAST_VISIBLE + 1;

        auto &menu = *m_menu_columns;
        wxASSERT(menu.GetMenuItemCount() == cnt);

        auto &view = *m_treeListCtrl->GetDataView();
        wxASSERT(cnt < view.GetColumnCount());

        for (auto pos = 0; pos < cnt; ++pos) {
                auto col = view.GetColumn(pos);
                auto title = col->GetTitle();

                if (auto item = menu.FindItemByPosition(pos); item->GetItemLabel() != title) {
                        item->SetItemLabel(title);
                }
        }
}

wxDataViewColumn* MainFrame::find_column(_In_ const wxString &title) const noexcept
{
        auto &view = *m_treeListCtrl->GetDataView();

        for (auto n = view.GetColumnCount(), pos = 0U; pos < n; ++pos) {
                if (auto col = view.GetColumn(pos); col->GetTitle() == title) {
                        return col;
                }
        }

        wxLogDebug(_("%s: column '%s' not found"), wxString::FromAscii(__func__), title);
        return nullptr;
}

wxDataViewColumn* MainFrame::find_column(_In_ int item_id) const noexcept
{
        if (auto item = m_menu_columns->FindItem(item_id)) {
                auto title = item->GetItemLabel(); 
                return find_column(title);
        }

        wxLogDebug(_("%s: item id '%d' not found"), wxString::FromAscii(__func__), item_id);
        return nullptr;
}

void MainFrame::on_view_column_update_ui(wxUpdateUIEvent &event)
{
        if (auto id = event.GetId(); auto col = find_column(id)) {
                event.Check(col->IsShown());
        }
}

void MainFrame::on_view_column(wxCommandEvent &event)
{
        if (auto id = event.GetId(); auto col = find_column(id)) {
                bool checked = event.GetInt();
                col->SetHidden(!checked);
        }
}

void MainFrame::on_item_activated(wxTreeListEvent &event)
{
        auto &tree = *m_treeListCtrl;
        
        if (auto item = event.GetItem(); tree.GetItemParent(item) == tree.GetRootItem()) {
                // item is a server
        } else if (auto state = tree.GetItemText(item, COL_STATE);
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
        auto str = tree.GetItemText(dev, COL_PORT);

        int port;
        return str.ToInt(&port) ? port : 0;
}

void MainFrame::on_toogle_persistent(wxCommandEvent&)
{
        for (auto &tree = *m_treeListCtrl; auto &item: get_selections(tree)) {

                if (tree.GetItemParent(item) != tree.GetRootItem()) { // server
                        auto ok = is_persistent(item);
                        set_persistent(item, !ok);
                }
        }
}

void MainFrame::on_save(wxCommandEvent&)
{
        auto &cfg = *wxConfig::Get();
        auto path = cfg.GetPath();

        cfg.DeleteGroup(g_key_devices);
        cfg.SetPath(g_key_devices);

        std::vector<device_location> persistent;

        int cnt = 0;
        auto &tree = *m_treeListCtrl;

        for (auto &item: get_selections(tree)) {

                auto parent = tree.GetItemParent(item);
                if (parent == tree.GetRootItem()) { // server
                        continue;
                }

                cfg.SetPath(wxString::Format(L"%d", ++cnt));

                auto url = tree.GetItemText(parent); // server
                cfg.Write(g_key_url, url);

                for (auto [key, col] : get_saved_keys()) {
                        auto value = tree.GetItemText(item, col);
                        cfg.Write(key, value);
                }

                if (is_persistent(item)) {
                        persistent.emplace_back(make_device_location(tree, parent, item));
                }

                cfg.SetPath(L"..");
        }

        cfg.Flush();
        cfg.SetPath(path);

        wxLogStatus(_("%d device(s) saved"), cnt);

        if (!cnt && has_items(tree)) {
                wxMessageBox(_("No selections were made"), _("Nothing to save"), wxICON_WARNING);
        }

        if (!vhci::set_persistent(get_vhci().get(), persistent)) {
                auto err = GetLastError();
                wxLogError(_("Cannot save persistent info\nError %#lx\n%s"), err, GetLastErrorMsg(err));
        }
}

void MainFrame::on_load(wxCommandEvent&)
{
        auto &tree = *m_treeListCtrl;
        int cnt = 0;

        auto persistent = get_persistent();
        
        for (auto &dc: get_saved()) {

                auto flags = get_saved_flags();
                auto [item, added] = find_or_add_device(dc);

                if (added || is_empty(dc)) {
                        constexpr auto state_flag = mkflag(COL_STATE);
                        static_assert(!(get_saved_flags() & state_flag));

                        dc[COL_STATE] = _(vhci::get_state_str(state::unplugged));
                        flags = set_persistent_notes(dc, flags | state_flag, persistent);

                        if (auto server = tree.GetItemParent(item); !tree.IsExpanded(server)) {
                                tree.Expand(server);
                        }
                } else {
                        wxLogVerbose(_("Skip loading existing device %s/%s"), get_url(dc), dc[COL_BUSID]);
                }

                update_device(item, dc, flags);
                ++cnt;
        }

        wxLogStatus(_("%d device(s) loaded"), cnt);
}

void MainFrame::on_refresh(wxCommandEvent &event)
{
        wxLogVerbose(wxString::FromAscii(__func__));

        auto &tree = *m_treeListCtrl;
        tree.DeleteAllItems();

        bool ok{};
        auto devices = vhci::get_imported_devices(get_vhci().get(), ok);
        if (!ok) {
                auto err = GetLastError();
                wxLogError(_("Cannot get imported devices\nError %#lx\n%s"), err, GetLastErrorMsg(err));
                return;
        }

        auto persistent = get_persistent();
        auto saved = as_set(get_saved());

        for (auto &dev: devices) {
                auto [item, added] = find_or_add_device(dev.location);
                wxASSERT(added);
                
                device_state st { 
                        .device = std::move(dev), 
                        .state = state::plugged 
                };

                auto [dc, flags] = make_device_columns(st);

                static_assert(get_saved_flags() & mkflag(COL_NOTES));
                flags = set_persistent_notes(dc, flags, persistent, &saved);

                update_device(item, dc, flags);

                if (auto server = tree.GetItemParent(item); !tree.IsExpanded(server)) {
                        tree.Expand(server);
                }
        }

        if (static bool once; !once) {
                once = true;
                on_load(event);
        }
}

