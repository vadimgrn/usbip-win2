///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.0.0-eea61759)
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
#include <wx/textctrl.h>
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
			ID_LOG_TOGGLE = 1000,
			ID_LOGLEVEL_ERROR,
			ID_LOGLEVEL_WARNING,
			ID_LOGLEVEL_MESSAGE,
			ID_LOGLEVEL_STATUS,
			ID_LOGLEVEL_INFO,
			ID_COL_BUSID,
			ID_COL_PORT,
			ID_COL_SPEED,
			ID_COL_VENDOR,
			ID_COL_PRODUCT,
			ID_COL_STATE,
			ID_COL_PERSISTENT,
			ID_COL_COMMENTS
		};

		wxStatusBar* m_statusBar;
		wxMenuBar* m_menubar;
		wxMenu* m_menu_file;
		wxMenu* m_menu_devices;
		wxMenu* m_menu_log;
		wxMenu* m_menu_view;
		wxMenu* m_menu_columns;
		wxMenu* m_menu_log_help;
		wxAuiToolBar* m_auiToolBar;
		wxAuiToolBarItem* m_tool_refresh;
		wxAuiToolBarItem* m_tool_attach;
		wxAuiToolBarItem* m_tool_detach;
		wxAuiToolBarItem* m_tool_detach_all;
		wxAuiToolBar* m_auiToolBarAdd;
		wxStaticText* m_staticTextServer;
		wxTextCtrl* m_textCtrlServer;
		wxStaticText* m_staticTextPort;
		wxSpinCtrl* m_spinCtrlPort;
		wxButton* m_button_add;
		wxTreeListCtrl* m_treeListCtrl;

		// Virtual event handlers, override them in your derived class
		virtual void on_close( wxCloseEvent& event ) { event.Skip(); }
		virtual void on_exit( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_refresh( wxCommandEvent& event ) { event.Skip(); }
		virtual void add_exported_devices( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_attach( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_detach( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_detach_all( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_log_show( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_log_show_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_log_level( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_column( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_column_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_view_labels( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_view_labels_update_ui( wxUpdateUIEvent& event ) { event.Skip(); }
		virtual void on_help_about( wxCommandEvent& event ) { event.Skip(); }
		virtual void on_item_activated( wxTreeListEvent& event ) { event.Skip(); }


	public:

		Frame( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = wxEmptyString, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 680,462 ), long style = wxDEFAULT_FRAME_STYLE|wxTAB_TRAVERSAL );
		wxAuiManager m_mgr;

		~Frame();

};

