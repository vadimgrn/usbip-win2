/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wxutils.h"
#include "utils.h"

#include <libusbip/win_handle.h>

#include <wx/msgdlg.h>
#include <wx/menu.h>
#include <wx/log.h>

#include <thread>

namespace
{

auto wait(_In_ HANDLE evt, _In_ DWORD timeout)
{
        bool ok{};

        switch (auto ret = WaitForSingleObject(evt, timeout)) {
        case WAIT_OBJECT_0:
                ok = true;
                break;
        case WAIT_TIMEOUT:
                break;
        default:
                wxASSERT(ret == WAIT_FAILED);
                auto err = GetLastError();
                wxLogVerbose(_("WaitForSingleObject error %lu\n%s"), err, wxSysErrorMsg(err));
        }

        return ok;
}

} // namespace


wxMenuItem* clone_menu_item(_In_ wxMenu &dest, _In_ int item_id, _In_ const wxMenu &src)
{
        wxMenuItem *clone{};

        if (item_id == wxID_SEPARATOR) {
                dest.AppendSeparator();
        } else if (auto item = src.FindItem(item_id)) {
                clone = dest.Append(item_id, item->GetItemLabel(), item->GetHelp(), item->GetKind());
                clone->Enable(item->IsEnabled());
                for (auto checked: {false, true}) {
                        clone->SetBitmap(item->GetBitmap(checked), checked);
                }
        } else {
                wxFAIL_MSG("FindItem");
        }

        return clone;
}

bool usbip::cancel_vhci_io()
{
        return vhci::cancel_io(get_vhci().get());
}

/*
 * Native Windows implementation uses MessageBox to show dialog. Because of this, 
 * idle events will not be handled and wxLogXXX output will be flushed 
 * when the dialog is closed. Moreover, the app will not show device state changes.
 * 
 * if (auto msgbox = FindWindowEx(dlg.GetHWND(), nullptr, L"#32770", caption.wc_str())) { // find MessageBox window
 *         PostMessage(msgbox, WM_CLOSE, 0, 0); // the same as cancel
 * }
 * 
 * @see src/msw/msgdlg.cpp, wxGenericMessageDialog
 * @see src/msw/dialog.cpp, wxMessageDialog
 */
void usbip::run_cancellable(
        _In_ wxWindow *parent,
        _In_ const wxString &msg,
        _In_ const wxString &caption,
        _In_ std::function<void(std::stop_token)> func,
        _In_ const std::function<cancel_function> &cancel)
{
        constexpr auto style = wxOK | wxICON_WARNING | wxCENTER | wxSTAY_ON_TOP | wxBORDER_NONE | wxPOPUP_WINDOW;

        wxGenericMessageDialog dlg(parent, msg, caption, style);
        dlg.SetOKLabel(_("&Cancel"));

        NullableHandle evt(CreateEvent(nullptr, true, false, nullptr));
        if (!evt) {
                auto err = GetLastError();
                wxLogError(_("Cannot create event\nError %lu\n%s"), err, wxSysErrorMsg(err));
                return;
        }

        auto f = [&dlg, evt = evt.get(), func = std::move(func)] (std::stop_token st)
        {
                try {
                        func(st);
                } catch (std::exception &e) {
                        wxLogVerbose(_("exception: %s"), what(e));
                }

                [[maybe_unused]] auto ok = SetEvent(evt);
                wxASSERT(ok);

                dlg.CallAfter(&wxGenericMessageDialog::EndModal, 0); // QueueEvent to GUI thread, see ShowModal()
        };

        std::jthread thread(std::move(f));

        {
                wxWindowDisabler wd; // ShowModal uses it too that creates issues
                if (wait(evt.get(), 1'000)) { // use MsgWaitForMultipleObjects if GUI thread is blocked for a long time
                        return;
                }
        }

        if (wait(evt.get(), 0)) {
                return;
        }

        if (auto done = !dlg.ShowModal() || wait(evt.get(), 0); !done) { // cancelled by user
                thread.request_stop();

                if (cancel && !cancel()) {
                        auto err = GetLastError();
                        wxLogVerbose(_("Could not cancel '%s', error %lu\n%s"), caption, err, wxSysErrorMsg(err));
                }
        }
}
