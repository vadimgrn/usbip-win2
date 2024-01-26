///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.0.0-eea61759)
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
	#ifdef __WXMSW__
	m_file_exit->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR( wxART_QUIT ), wxASCII_STR( wxART_MENU )) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_file_exit->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_QUIT ), wxASCII_STR( wxART_MENU )) );
	#endif
	m_menu_file->Append( m_file_exit );

	m_menubar->Append( m_menu_file, _("File") );

	m_menu_devices = new wxMenu();
	wxMenuItem* m_cmd_refresh;
	m_cmd_refresh = new wxMenuItem( m_menu_devices, wxID_REFRESH, wxString( _("Refresh") ) + wxT('\t') + wxT("CTRL+R"), _("Refresh list of remote devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_refresh->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR( wxART_REDO ), wxASCII_STR( wxART_MENU )) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_refresh->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_REDO ), wxASCII_STR( wxART_MENU )) );
	#endif
	m_menu_devices->Append( m_cmd_refresh );

	wxMenuItem* m_cmd_add;
	m_cmd_add = new wxMenuItem( m_menu_devices, wxID_ANY, wxString( _("Add") ) + wxT('\t') + wxT("CTRL+F"), _("Add remote devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_add->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR( wxART_PLUS ), wxASCII_STR( wxART_MENU )), wxNullBitmap );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_add->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_PLUS ), wxASCII_STR( wxART_MENU )) );
	#endif
	m_menu_devices->Append( m_cmd_add );

	m_menu_devices->AppendSeparator();

	wxMenuItem* m_cmd_attach;
	m_cmd_attach = new wxMenuItem( m_menu_devices, wxID_ANY, wxString( _("Attach") ) + wxT('\t') + wxT("CTRL+A"), _("Attach remote device(s)"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_attach->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR( wxART_TICK_MARK ), wxASCII_STR( wxART_MENU )) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_attach->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_TICK_MARK ), wxASCII_STR( wxART_MENU )) );
	#endif
	m_menu_devices->Append( m_cmd_attach );

	wxMenuItem* m_cmd_detach;
	m_cmd_detach = new wxMenuItem( m_menu_devices, wxID_ANY, wxString( _("Detach") ) + wxT('\t') + wxT("CTRL+D"), _("Detach remote device(s)"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_detach->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR( wxART_CROSS_MARK ), wxASCII_STR( wxART_MENU )) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_detach->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_CROSS_MARK ), wxASCII_STR( wxART_MENU )) );
	#endif
	m_menu_devices->Append( m_cmd_detach );

	m_menu_devices->AppendSeparator();

	wxMenuItem* m_cmd_detach_all;
	m_cmd_detach_all = new wxMenuItem( m_menu_devices, wxID_ANY, wxString( _("Detach All") ) , _("Detach all remote devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_detach_all->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR( wxART_DELETE ), wxASCII_STR( wxART_MENU )) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_detach_all->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_DELETE ), wxASCII_STR( wxART_MENU )) );
	#endif
	m_menu_devices->Append( m_cmd_detach_all );

	m_menubar->Append( m_menu_devices, _("Devices") );

	m_menu_log = new wxMenu();
	wxMenuItem* m_log_toggle;
	m_log_toggle = new wxMenuItem( m_menu_log, ID_LOG_TOGGLE, wxString( _("Toggle window") ) + wxT('\t') + wxT("CTRL+L"), _("Show/hide the window with log records"), wxITEM_CHECK );
	#ifdef __WXMSW__
	m_log_toggle->SetBitmaps( wxNullBitmap );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_log_toggle->SetBitmap( wxNullBitmap );
	#endif
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

	m_menu_view = new wxMenu();
	m_menu_columns = new wxMenu();
	wxMenuItem* m_menu_columnsItem = new wxMenuItem( m_menu_view, wxID_ANY, _("Columns"), wxEmptyString, wxITEM_NORMAL, m_menu_columns );
	#if (defined( __WXMSW__ ) || defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_menu_columnsItem->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_REPORT_VIEW ), wxASCII_STR( wxART_MENU )) );
	#endif

	wxMenuItem* m_view_busid;
	m_view_busid = new wxMenuItem( m_menu_columns, ID_COL_BUSID, wxString( _("Bus-Id") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_busid );
	m_view_busid->Check( true );

	wxMenuItem* m_view_port;
	m_view_port = new wxMenuItem( m_menu_columns, ID_COL_PORT, wxString( _("Port") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_port );
	m_view_port->Enable( false );
	m_view_port->Check( true );

	wxMenuItem* m_view_speed;
	m_view_speed = new wxMenuItem( m_menu_columns, ID_COL_SPEED, wxString( _("Speed") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_speed );
	m_view_speed->Check( true );

	wxMenuItem* m_view_vendor;
	m_view_vendor = new wxMenuItem( m_menu_columns, ID_COL_VENDOR, wxString( _("Vendor") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_vendor );
	m_view_vendor->Check( true );

	wxMenuItem* m_view_product;
	m_view_product = new wxMenuItem( m_menu_columns, ID_COL_PRODUCT, wxString( _("Product") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_product );
	m_view_product->Check( true );

	wxMenuItem* m_view_state;
	m_view_state = new wxMenuItem( m_menu_columns, ID_COL_STATE, wxString( _("State") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_state );
	m_view_state->Enable( false );
	m_view_state->Check( true );

	wxMenuItem* m_view_persistent;
	m_view_persistent = new wxMenuItem( m_menu_columns, ID_COL_PERSISTENT, wxString( _("AA") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_persistent );
	m_view_persistent->Check( true );

	wxMenuItem* m_view_comment;
	m_view_comment = new wxMenuItem( m_menu_columns, ID_COL_COMMENTS, wxString( _("Comments") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_comment );
	m_view_comment->Check( true );

	m_menu_view->Append( m_menu_columnsItem );

	m_menu_view->AppendSeparator();

	wxMenuItem* m_view_toolbar;
	m_view_toolbar = new wxMenuItem( m_menu_view, wxID_ANY, wxString( _("Toolbar") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_view->Append( m_view_toolbar );
	m_view_toolbar->Check( true );

	wxMenuItem* m_view_toolbar_add;
	m_view_toolbar_add = new wxMenuItem( m_menu_view, wxID_ANY, wxString( _("Add") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_view->Append( m_view_toolbar_add );
	m_view_toolbar_add->Check( true );

	wxMenuItem* m_view_labels;
	m_view_labels = new wxMenuItem( m_menu_view, wxID_ANY, wxString( _("Labels") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_view->Append( m_view_labels );
	m_view_labels->Check( true );

	m_menubar->Append( m_menu_view, _("View") );

	m_menu_log_help = new wxMenu();
	wxMenuItem* m_help_about;
	m_help_about = new wxMenuItem( m_menu_log_help, wxID_ABOUT, wxString( _("About") ) , wxEmptyString, wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_help_about->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR( wxART_INFORMATION ), wxASCII_STR( wxART_MENU )) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_help_about->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_INFORMATION ), wxASCII_STR( wxART_MENU )) );
	#endif
	m_menu_log_help->Append( m_help_about );

	m_menubar->Append( m_menu_log_help, _("Help") );

	this->SetMenuBar( m_menubar );

	m_auiToolBar = new wxAuiToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_DEFAULT_STYLE|wxAUI_TB_GRIPPER|wxAUI_TB_TEXT );
	m_auiToolBar->SetToolSeparation( 1 );
	m_tool_refresh = m_auiToolBar->AddTool( wxID_ANY, _("Refresh"), wxArtProvider::GetBitmap( wxASCII_STR( wxART_REDO ), wxASCII_STR( wxART_TOOLBAR )), wxNullBitmap, wxITEM_NORMAL, _("Refresh list of remote devices"), wxEmptyString, NULL );

	m_tool_attach = m_auiToolBar->AddTool( wxID_ANY, _("Attach"), wxArtProvider::GetBitmap( wxASCII_STR( wxART_TICK_MARK ), wxASCII_STR( wxART_TOOLBAR )), wxNullBitmap, wxITEM_NORMAL, _("Attach remote device(s)"), wxEmptyString, NULL );

	m_tool_detach = m_auiToolBar->AddTool( wxID_ANY, _("Detach"), wxArtProvider::GetBitmap( wxASCII_STR( wxART_CROSS_MARK ), wxASCII_STR( wxART_TOOLBAR )), wxNullBitmap, wxITEM_NORMAL, _("Detach remote device(s)"), wxEmptyString, NULL );

	m_tool_detach_all = m_auiToolBar->AddTool( wxID_ANY, _("Detach All"), wxArtProvider::GetBitmap( wxASCII_STR( wxART_DELETE ), wxASCII_STR( wxART_TOOLBAR )), wxNullBitmap, wxITEM_NORMAL, _("Detach all remote devices"), wxEmptyString, NULL );

	m_auiToolBar->Realize();
	m_mgr.AddPane( m_auiToolBar, wxAuiPaneInfo() .Top() .CaptionVisible( false ).CloseButton( false ).Gripper().Dock().Resizable().FloatingSize( wxSize( -1,-1 ) ).Row( 0 ).Layer( 10 ).ToolbarPane() );

	m_auiToolBarAdd = new wxAuiToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_DEFAULT_STYLE|wxAUI_TB_GRIPPER|wxTAB_TRAVERSAL );
	m_auiToolBarAdd->SetToolPacking( 10 );
	m_staticTextServer = new wxStaticText( m_auiToolBarAdd, wxID_ANY, _("Server"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticTextServer->Wrap( -1 );
	m_staticTextServer->SetToolTip( _("USBIP server hostname or IP address") );

	m_auiToolBarAdd->AddControl( m_staticTextServer );
	m_textCtrlServer = new wxTextCtrl( m_auiToolBarAdd, wxID_ANY, _("pc"), wxDefaultPosition, wxDefaultSize, 0 );
	m_textCtrlServer->SetToolTip( _("Hostname or IP address") );

	m_auiToolBarAdd->AddControl( m_textCtrlServer );
	m_staticTextPort = new wxStaticText( m_auiToolBarAdd, wxID_ANY, _("Port"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticTextPort->Wrap( -1 );
	m_staticTextPort->SetToolTip( _("TCP/IP port number") );

	m_auiToolBarAdd->AddControl( m_staticTextPort );
	m_spinCtrlPort = new wxSpinCtrl( m_auiToolBarAdd, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT|wxSP_ARROW_KEYS|wxSP_WRAP, 1025, 65535, 0 );
	m_spinCtrlPort->SetToolTip( _("TCP/IP port number") );

	m_auiToolBarAdd->AddControl( m_spinCtrlPort );
	m_button_add = new wxButton( m_auiToolBarAdd, wxID_ADD, _("Add devices"), wxDefaultPosition, wxDefaultSize, 0 );

	m_button_add->SetDefault();

	m_button_add->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR( wxART_PLUS ), wxASCII_STR( wxART_BUTTON )) );
	m_auiToolBarAdd->AddControl( m_button_add );
	m_auiToolBarAdd->Realize();
	m_mgr.AddPane( m_auiToolBarAdd, wxAuiPaneInfo() .Top() .CaptionVisible( false ).CloseButton( false ).Gripper().Dock().Resizable().FloatingSize( wxSize( 137,137 ) ).Row( 0 ).Layer( 10 ).ToolbarPane() );

	m_treeListCtrl = new wxTreeListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTL_MULTIPLE );
	m_treeListCtrl->SetFont( wxFont( wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString ) );
	m_treeListCtrl->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

	m_mgr.AddPane( m_treeListCtrl, wxAuiPaneInfo() .Center() .Caption( _("USB devices") ).CloseButton( false ).PaneBorder( false ).Movable( false ).Dock().Resizable().FloatingSize( wxDefaultSize ).DockFixed( true ).BottomDockable( false ).TopDockable( false ).LeftDockable( false ).RightDockable( false ).Floatable( false ) );

	m_treeListCtrl->AppendColumn( _("Server / Bus-Id"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Port"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_RIGHT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("USB Speed"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Vendor"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Product"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("State"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("AA"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Comments"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Saved State"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_HIDDEN );


	m_mgr.Update();
	this->Centre( wxBOTH );

	// Connect Events
	this->Connect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( Frame::on_close ) );
	m_menu_file->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_exit ), this, m_file_exit->GetId());
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_refresh ), this, m_cmd_refresh->GetId());
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::add_exported_devices ), this, m_cmd_add->GetId());
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_attach ), this, m_cmd_attach->GetId());
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_detach ), this, m_cmd_detach->GetId());
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_detach_all ), this, m_cmd_detach_all->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_show ), this, m_log_toggle->GetId());
	this->Connect( m_log_toggle->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_show_update_ui ) );
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_error->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_warning->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_message->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_status->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_loglevel_info->GetId());
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_busid->GetId());
	this->Connect( m_view_busid->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_port->GetId());
	this->Connect( m_view_port->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_speed->GetId());
	this->Connect( m_view_speed->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_vendor->GetId());
	this->Connect( m_view_vendor->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_product->GetId());
	this->Connect( m_view_product->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_state->GetId());
	this->Connect( m_view_state->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_persistent->GetId());
	this->Connect( m_view_persistent->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_comment->GetId());
	this->Connect( m_view_comment->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_view->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_labels ), this, m_view_labels->GetId());
	this->Connect( m_view_labels->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_labels_update_ui ) );
	m_menu_log_help->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_help_about ), this, m_help_about->GetId());
	this->Connect( m_tool_refresh->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_refresh ) );
	this->Connect( m_tool_attach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_attach ) );
	this->Connect( m_tool_detach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach ) );
	this->Connect( m_tool_detach_all->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach_all ) );
	m_button_add->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( Frame::add_exported_devices ), NULL, this );
	m_treeListCtrl->Connect( wxEVT_TREELIST_ITEM_ACTIVATED, wxTreeListEventHandler( Frame::on_item_activated ), NULL, this );
}

Frame::~Frame()
{
	// Disconnect Events
	this->Disconnect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( Frame::on_close ) );
	this->Disconnect( ID_LOG_TOGGLE, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_show_update_ui ) );
	this->Disconnect( ID_COL_BUSID, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( ID_COL_PORT, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( ID_COL_SPEED, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( ID_COL_VENDOR, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( ID_COL_PRODUCT, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( ID_COL_STATE, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( ID_COL_PERSISTENT, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( ID_COL_COMMENTS, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_labels_update_ui ) );
	this->Disconnect( m_tool_refresh->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_refresh ) );
	this->Disconnect( m_tool_attach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_attach ) );
	this->Disconnect( m_tool_detach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach ) );
	this->Disconnect( m_tool_detach_all->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach_all ) );
	m_button_add->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( Frame::add_exported_devices ), NULL, this );
	m_treeListCtrl->Disconnect( wxEVT_TREELIST_ITEM_ACTIVATED, wxTreeListEventHandler( Frame::on_item_activated ), NULL, this );

	m_mgr.UnInit();

}
