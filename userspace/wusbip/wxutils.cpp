/*
 * Copyright (C) 2023 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "wxutils.h"
#include "utils.h"

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


std::strong_ordering operator <=> (_In_ const wxString &a, _In_ const wxString &b)
{
        std::strong_ordering v[] { 
                std::strong_ordering::less, 
                std::strong_ordering::equal, 
                std::strong_ordering::greater 
        };

        auto i = a.Cmp(b);
        wxASSERT(i >= -1 && i <= 1);

        return v[++i];
}

wxMenuItem* clone_menu_item(_In_ wxMenu &dest, _In_ int item_id, _In_ const wxMenu &src)
{
        wxMenuItem *clone{};

        if (item_id == wxID_SEPARATOR) {
                dest.AppendSeparator();
        } else if (auto item = src.FindItem(item_id)) {
                clone = dest.Append(item_id, item->GetItemLabel(), item->GetHelp(), item->GetKind());
                for (auto checked: {false, true}) {
                        clone->SetBitmap(item->GetBitmap(checked), checked);
                }
        } else {
                wxFAIL_MSG("FindItem");
        }

        return clone;
}

void usbip::cancel_synchronous_io(_In_ HANDLE thread)
{
        wxLogVerbose(wxString::FromAscii(__func__));

        if (!CancelSynchronousIo(thread)) {
                auto err = GetLastError();
                wxLogVerbose(_("CancelSynchronousIo error %lu\n%s"), err, wxSysErrorMsg(err));
        }
}

void usbip::cancel_connect(_In_ HANDLE thread)
{
        wxLogVerbose(wxString::FromAscii(__func__));

        if (!QueueUserAPC( [] (auto) { wxLogVerbose(L"APC"); }, thread, 0)) {
                auto err = GetLastError();
                wxLogVerbose(_("QueueUserAPC error %lu\n%s"), err, wxSysErrorMsg(err));
        }
}

/*
 * FIXME: 
 * cannot close dialog from the thread for wxMessageDialog, EndModal() fails on assertion that it must be IsModal().
 * Native Windows implementation uses MessageBox to show dialog, can't close it from the thread.
 * @see src/msw/msgdlg.cpp, wxGenericMessageDialog
 * @see src/msw/dialog.cpp, wxMessageDialog
 */
bool usbip::run_cancellable(
        _In_ wxWindow *parent,
        _In_ const wxString &msg,
        _In_ const wxString &caption,
        _In_ std::function<void()> func,
        _In_ const std::function<void(_In_ HANDLE thread)> &cancel)
{
        constexpr auto style = wxOK | wxICON_WARNING | wxCENTER | wxSTAY_ON_TOP | wxBORDER_NONE | wxPOPUP_WINDOW;

        wxGenericMessageDialog dlg(parent, msg, caption, style);
        dlg.SetOKLabel(_("&Cancel"));

        auto &evt = get_event();
        wxASSERT(evt); // checked in usbip::init(), utils.cpp

        [[maybe_unused]] auto ok = ResetEvent(evt.get());
        wxASSERT(ok);

        auto f = [&dlg, evt = evt.get(), func = std::move(func)]
        {
                func();

                [[maybe_unused]] auto ok = SetEvent(evt);
                wxASSERT(ok);

                dlg.CallAfter(&wxGenericMessageDialog::EndModal, 0); // QueueEvent to GUI thread, see ShowModal()
        };

        std::jthread thread(std::move(f));
        wxWindowDisabler dis;

        auto ret = 0;
        if (!wait(evt.get(), 1'000)) { // don't block GUI thread for a long time or use MsgWaitForMultipleObjects
                ret = dlg.ShowModal(); // executes event loop, processes CallAfter events
                if (ret == wxID_OK) { // cancelled by user
                        cancel(thread.native_handle());
                }
        }
        return ret;
}
