///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.0.0-0-g0efcecf)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "frame.h"

///////////////////////////////////////////////////////////////////////////

Frame::Frame( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxFrame( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxDefaultSize, wxDefaultSize );
	m_mgr.SetManagedWindow(this);
	m_mgr.SetFlags(wxAUI_MGR_DEFAULT);

	m_statusBar = this->CreateStatusBar( 1, wxSTB_SIZEGRIP, wxID_ANY );
	m_menubar = new wxMenuBar( 0 );
	m_menu_file = new wxMenu();
	wxMenuItem* m_file_exit;
	m_file_exit = new wxMenuItem( m_menu_file, wxID_EXIT, wxString( _("Exit") ) , wxEmptyString, wxITEM_NORMAL );
	m_menu_file->Append( m_file_exit );

	m_menubar->Append( m_menu_file, _("File") );

	m_menu_commands = new wxMenu();
	wxMenuItem* m_cmd_add;
	m_cmd_add = new wxMenuItem( m_menu_commands, wxID_ANY, wxString( _("Add") ) + wxT('\t') + wxT("CTRL+F"), _("Add exported USB devices on remote host"), wxITEM_NORMAL );
	m_menu_commands->Append( m_cmd_add );

	wxMenuItem* m_cmd_attach;
	m_cmd_attach = new wxMenuItem( m_menu_commands, wxID_ANY, wxString( _("Attach") ) + wxT('\t') + wxT("CTRL+A"), _("Attach an exportable USB device"), wxITEM_NORMAL );
	m_menu_commands->Append( m_cmd_attach );

	wxMenuItem* m_cmd_detach;
	m_cmd_detach = new wxMenuItem( m_menu_commands, wxID_ANY, wxString( _("Detach") ) + wxT('\t') + wxT("CTRL+D"), _("Detach an imported USB device"), wxITEM_NORMAL );
	m_menu_commands->Append( m_cmd_detach );

	wxMenuItem* m_cmd_refresh;
	m_cmd_refresh = new wxMenuItem( m_menu_commands, wxID_REFRESH, wxString( _("Refresh") ) + wxT('\t') + wxT("CTRL+R"), _("Refresh the list of imported USB devices"), wxITEM_NORMAL );
	m_menu_commands->Append( m_cmd_refresh );

	m_menubar->Append( m_menu_commands, _("Devices") );

	m_menu_log = new wxMenu();
	wxMenuItem* m_log_toggle;
	m_log_toggle = new wxMenuItem( m_menu_log, ID_LOG_TOGGLE, wxString( _("Toggle window") ) + wxT('\t') + wxT("CTRL+L"), _("Show/hide the window with log records"), wxITEM_CHECK );
	m_menu_log->Append( m_log_toggle );

	m_menu_log->AppendSeparator();

	wxMenuItem* m_loglevel_error;
	m_loglevel_error = new wxMenuItem( m_menu_log, ID_LOGLEVEL_ERROR, wxString( _("Error") ) , _("A serious error, user must be informed about it"), wxITEM_RADIO );
	m_menu_log->Append( m_loglevel_error );

	wxMenuItem* m_loglevel_warning;
	m_loglevel_warning = new wxMenuItem( m_menu_log, ID_LOGLEVEL_WARNING, wxString( _("Warning") ) , _("User is normally informed about it but may be ignored"), wxITEM_RADIO );
	m_menu_log->Append( m_loglevel_warning );

	wxMenuItem* m_loglevel_message;
	m_loglevel_message = new wxMenuItem( m_menu_log, ID_LOGLEVEL_MESSAGE, wxString( _("Message") ) , _("Normal message"), wxITEM_RADIO );
	m_menu_log->Append( m_loglevel_message );

	wxMenuItem* m_loglevel_status;
	m_loglevel_status = new wxMenuItem( m_menu_log, ID_LOGLEVEL_STATUS, wxString( _("Status") ) , _("Informational: might go to the status bar"), wxITEM_RADIO );
	m_menu_log->Append( m_loglevel_status );

	wxMenuItem* m_loglevel_info;
	m_loglevel_info = new wxMenuItem( m_menu_log, ID_LOGLEVEL_INFO, wxString( _("Verbose") ) , _("Displayed only in the log window"), wxITEM_RADIO );
	m_menu_log->Append( m_loglevel_info );

	m_menubar->Append( m_menu_log, _("Log") );

	m_menu_log_help = new wxMenu();
	wxMenuItem* m_help_about;
	m_help_about = new wxMenuItem( m_menu_log_help, wxID_ABOUT, wxString( _("About") ) , wxEmptyString, wxITEM_NORMAL );
	m_menu_log_help->Append( m_help_about );

	m_menubar->Append( m_menu_log_help, _("Help") );

	this->SetMenuBar( m_menubar );

	m_auiToolBar = new wxAuiToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT );
	m_toolPort = m_auiToolBar->AddTool( wxID_ANY, _("Port"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL );

	m_toolAttach = m_auiToolBar->AddTool( wxID_ANY, _("Attach"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL );

	m_toolDetach = m_auiToolBar->AddTool( wxID_ANY, _("Detach"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL );

	m_auiToolBar->Realize();
	m_mgr.AddPane( m_auiToolBar, wxAuiPaneInfo() .Left() .PinButton( true ).Dock().Resizable().FloatingSize( wxSize( 95,141 ) ).Row( 0 ).Layer( 2 ) );

	m_auiToolBarAdd = new wxAuiToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT );
	m_auiToolBarAdd->SetToolPacking( 10 );
	m_staticTextServer = new wxStaticText( m_auiToolBarAdd, wxID_ANY, _("Server"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticTextServer->Wrap( -1 );
	m_staticTextServer->SetToolTip( _("USBIP server hostname or IP address") );

	m_auiToolBarAdd->AddControl( m_staticTextServer );
	m_textCtrlServer = new wxTextCtrl( m_auiToolBarAdd, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	m_textCtrlServer->SetToolTip( _("Hostname or IP address") );

	m_auiToolBarAdd->AddControl( m_textCtrlServer );
	m_staticTextPort = new wxStaticText( m_auiToolBarAdd, wxID_ANY, _("Port"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticTextPort->Wrap( -1 );
	m_staticTextPort->SetToolTip( _("TCP/IP port number") );

	m_auiToolBarAdd->AddControl( m_staticTextPort );
	m_spinCtrlPort = new wxSpinCtrl( m_auiToolBarAdd, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT|wxSP_ARROW_KEYS|wxSP_WRAP, 1025, 65535, 0 );
	m_spinCtrlPort->SetToolTip( _("TCP/IP port number") );

	m_auiToolBarAdd->AddControl( m_spinCtrlPort );
	m_button_add = new wxButton( m_auiToolBarAdd, wxID_ADD, _("Add"), wxDefaultPosition, wxDefaultSize, 0 );
	m_auiToolBarAdd->AddControl( m_button_add );
	m_auiToolBarAdd->Realize();
	m_mgr.AddPane( m_auiToolBarAdd, wxAuiPaneInfo() .Bottom() .Caption( _("Add exported devices") ).PinButton( true ).Dock().Resizable().FloatingSize( wxSize( 137,137 ) ).Row( 0 ).Layer( 1 ) );

	m_treeListCtrl = new wxTreeListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTL_CHECKBOX|wxTL_DEFAULT_STYLE );
	m_treeListCtrl->SetFont( wxFont( wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString ) );
	m_treeListCtrl->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

	m_mgr.AddPane( m_treeListCtrl, wxAuiPaneInfo() .Center() .Caption( _("Imported USB devices") ).CloseButton( false ).Movable( false ).Dock().Resizable().FloatingSize( wxDefaultSize ).DockFixed( true ).BottomDockable( false ).TopDockable( false ).LeftDockable( false ).RightDockable( false ).Floatable( false ) );

	m_treeListCtrl->AppendColumn( _("Server / Bus-Id"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Port"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_RIGHT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("USB Speed"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Vendor"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Product"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("State"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Persistent"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_HIDDEN );


	m_mgr.Update();
	this->Centre( wxBOTH );

	// Connect Events
	this->Connect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( Frame::on_close ) );
	m_menu_file->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_exit ), this, m_file_exit->GetId());
	m_menu_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::add_exported_devices ), this, m_cmd_add->GetId());
	m_menu_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_attach ), this, m_cmd_attach->GetId());
	m_menu_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_detach ), this, m_cmd_detach->GetId());
	m_menu_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_refresh ), this, m_cmd_refresh->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_show ), this, m_log_toggle->GetId());
	this->Connect( m_log_toggle->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_show_update_ui ) );
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_error->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_warning->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_message->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_status->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_info->GetId());
	m_menu_log_help->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_help_about ), this, m_help_about->GetId());
	this->Connect( m_toolPort->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_port ) );
	this->Connect( m_toolAttach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_attach ) );
	this->Connect( m_toolDetach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach ) );
	m_button_add->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( Frame::add_exported_devices ), NULL, this );
	m_treeListCtrl->Connect( wxEVT_TREELIST_ITEM_CHECKED, wxTreeListEventHandler( Frame::on_tree_item_checked ), NULL, this );
}

Frame::~Frame()
{
	// Disconnect Events
	this->Disconnect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( Frame::on_close ) );
	this->Disconnect( ID_LOG_TOGGLE, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_show_update_ui ) );
	this->Disconnect( m_toolPort->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_port ) );
	this->Disconnect( m_toolAttach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_attach ) );
	this->Disconnect( m_toolDetach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach ) );
	m_button_add->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( Frame::add_exported_devices ), NULL, this );
	m_treeListCtrl->Disconnect( wxEVT_TREELIST_ITEM_CHECKED, wxTreeListEventHandler( Frame::on_tree_item_checked ), NULL, this );

	m_mgr.UnInit();

}
