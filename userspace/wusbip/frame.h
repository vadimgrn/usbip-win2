///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.1.0-0-g733bf3d)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/statusbr.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/menu.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/spinctrl.h>
#include <wx/button.h>
#include <wx/treelist.h>
#include <wx/frame.h>

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
/// Class Frame
///////////////////////////////////////////////////////////////////////////////
class Frame : public wxFrame
{
	private:

	protected:
		enum
		{
			ID_TOGGLE_AUTO = 1000,
			ID_EDIT_NOTES,
			ID_CLOSE_TO_TRAY,
			ID_ATTACH,
			ID_DETACH,
			ID_TOGGLE_LOG_WINDOW
		};

		wxStatusBar* m_statusBar;
		wxMenuBar* m_menubar;
		wxMenu* m_menu_file;
		wxMenu* m_menu_edit;
		wxMenu* m_menu_view;
		wxMenu* m_menu_columns;
		wxMenu* m_menu_devices;
		wxMenu* m_menu_log;
		wxMenu* m_menu_log_help;
		wxAuiToolBar* m_auiToolBar;
		wxAuiToolBarItem* m_tool_reload;
		wxAuiToolBarItem* m_tool_attach;
		wxAuiToolBarItem* m_tool_detach;
		wxAuiToolBarItem* m_tool_detach_all;
		wxAuiToolBarItem* m_tool_load;
		wxAuiToolBarItem* m_tool_save;
		wxAuiToolBar* m_auiToolBarAdd;
		wxStaticText* m_staticTextServer;
		wxComboBox* m_comboBoxServer;
		wxStaticText* m_staticTextPort;
		wxSpinCtrl* m_spinCtrlPort;
		wxButton* m_button_add;
		wxTreeListCtrl* m_treeListCtrl;

		// Virtual event handlers, override them in your derived class
		virtual void on_close( wxCloseEvent& event ) { event.Skip(); }
		virtual void on_frame_mouse_wheel( wxMouseEvent& event ) { event.Skip(); }
		virtual void on_load( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_save( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_save_selected( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_has_selected_devices_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_exit( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_select_all( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_has_devices_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_copy_rows( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_toogle_auto( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_edit_notes( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_edit_notes_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_view_column( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_column_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_view_zebra( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_zebra_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_view_labels( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_labels_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_view_font_increase( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_font_decrease( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_font_default( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_reset( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_reload( wxCommandEvent& event ) { event.Skip(); }
		virtual void add_exported_devices( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_attach( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_detach( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_detach_all( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_log_show( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_log_show_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_log_verbose( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_log_verbose_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_help_about( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_help_about_lib( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_item_activated( wxTreeListEvent& event ) { event.Skip(); }
		virtual void on_item_context_menu( wxTreeListEvent& event ) { event.Skip(); }


	public:

		Frame( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxEmptyString, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 752,462 ), long style = wxDEFAULT_FRAME_STYLE|wxTAB_TRAVERSAL );
		wxAuiManager m_mgr;

		~Frame();

};

