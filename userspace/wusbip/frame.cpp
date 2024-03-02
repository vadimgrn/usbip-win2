///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version 4.1.0-0-g733bf3d)
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
	wxMenuItem* m_file_load;
	m_file_load = new wxMenuItem( m_menu_file, wxID_ANY, wxString( _("Load") ) + wxT('\t') + wxT("CTRL+L"), _("Load saved devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_file_load->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_PASTE), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_file_load->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_PASTE), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_file->Append( m_file_load );

	wxMenuItem* m_file_save;
	m_file_save = new wxMenuItem( m_menu_file, wxID_SAVE, wxString( _("Save") ) + wxT('\t') + wxT("CTRL+S"), _("Save devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_file_save->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_FILE_SAVE), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_file_save->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_FILE_SAVE), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_file->Append( m_file_save );

	wxMenuItem* m_file_save_selected;
	m_file_save_selected = new wxMenuItem( m_menu_file, wxID_SAVEAS, wxString( _("Save selected") ) + wxT('\t') + wxT("ALT+S"), _("Save selected devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_file_save_selected->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_FILE_SAVE_AS), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_file_save_selected->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_FILE_SAVE_AS), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_file->Append( m_file_save_selected );

	m_menu_file->AppendSeparator();

	wxMenuItem* m_file_exit;
	m_file_exit = new wxMenuItem( m_menu_file, wxID_EXIT, wxString( _("E&xit") ) , wxEmptyString, wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_file_exit->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_QUIT), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_file_exit->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_QUIT), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_file->Append( m_file_exit );

	m_menubar->Append( m_menu_file, _("File") );

	m_menu_edit = new wxMenu();
	wxMenuItem* m_select_all;
	m_select_all = new wxMenuItem( m_menu_edit, wxID_SELECTALL, wxString( _("Select all") ) + wxT('\t') + wxT("CTRL+A"), _("Select all devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_select_all->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_FULL_SCREEN), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_select_all->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_FULL_SCREEN), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_edit->Append( m_select_all );

	m_menu_edit->AppendSeparator();

	wxMenuItem* m_toggle_auto;
	m_toggle_auto = new wxMenuItem( m_menu_edit, wxID_ANY, wxString( _("Toggle Auto") ) + wxT('\t') + wxT("CTRL+P"), _("Toggle Auto (aka Persistent)"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_toggle_auto->SetBitmaps( wxNullBitmap );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_toggle_auto->SetBitmap( wxNullBitmap );
	#endif
	m_menu_edit->Append( m_toggle_auto );

	wxMenuItem* m_edit_notes;
	m_edit_notes = new wxMenuItem( m_menu_edit, wxID_ANY, wxString( _("Notes") ) + wxT('\t') + wxT("CTRL+N"), _("Edit notes for a device"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_edit_notes->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_EDIT), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_edit_notes->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_EDIT), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_edit->Append( m_edit_notes );

	m_menubar->Append( m_menu_edit, _("Edit") );

	m_menu_view = new wxMenu();
	m_menu_columns = new wxMenu();
	wxMenuItem* m_menu_columnsItem = new wxMenuItem( m_menu_view, wxID_ANY, _("Columns"), wxEmptyString, wxITEM_NORMAL, m_menu_columns );
	#if (defined( __WXMSW__ ) || defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_menu_columnsItem->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_REPORT_VIEW), wxASCII_STR(wxART_MENU) ) );
	#endif

	wxMenuItem* m_view_busid;
	m_view_busid = new wxMenuItem( m_menu_columns, wxID_ANY, wxString( _("?") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_busid );
	m_view_busid->Check( true );

	wxMenuItem* m_view_port;
	m_view_port = new wxMenuItem( m_menu_columns, wxID_ANY, wxString( _("?") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_port );
	m_view_port->Enable( false );
	m_view_port->Check( true );

	wxMenuItem* m_view_speed;
	m_view_speed = new wxMenuItem( m_menu_columns, wxID_ANY, wxString( _("?") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_speed );
	m_view_speed->Check( true );

	wxMenuItem* m_view_vendor;
	m_view_vendor = new wxMenuItem( m_menu_columns, wxID_ANY, wxString( _("?") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_vendor );
	m_view_vendor->Check( true );

	wxMenuItem* m_view_product;
	m_view_product = new wxMenuItem( m_menu_columns, wxID_ANY, wxString( _("?") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_product );
	m_view_product->Check( true );

	wxMenuItem* m_view_state;
	m_view_state = new wxMenuItem( m_menu_columns, wxID_ANY, wxString( _("?") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_state );
	m_view_state->Enable( false );
	m_view_state->Check( true );

	wxMenuItem* m_view_persistent;
	m_view_persistent = new wxMenuItem( m_menu_columns, wxID_ANY, wxString( _("?") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_persistent );
	m_view_persistent->Check( true );

	wxMenuItem* m_view_notes;
	m_view_notes = new wxMenuItem( m_menu_columns, wxID_ANY, wxString( _("?") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_columns->Append( m_view_notes );
	m_view_notes->Check( true );

	m_menu_view->Append( m_menu_columnsItem );

	wxMenuItem* m_view_zebra;
	m_view_zebra = new wxMenuItem( m_menu_view, wxID_ANY, wxString( _("Alternate row colour") ) , _("Alternate row colour for the tree"), wxITEM_CHECK );
	m_menu_view->Append( m_view_zebra );

	wxMenuItem* m_view_labels;
	m_view_labels = new wxMenuItem( m_menu_view, wxID_ANY, wxString( _("Show toolbar labels") ) , wxEmptyString, wxITEM_CHECK );
	m_menu_view->Append( m_view_labels );
	m_view_labels->Check( true );

	m_menu_view->AppendSeparator();

	wxMenuItem* m_view_font_increase;
	m_view_font_increase = new wxMenuItem( m_menu_view, ID_FONT_INCREASE, wxString( _("Increase font") ) + wxT('\t') + wxT("CTRL++"), _("Increase font size, Ctrl+Wheel"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_view_font_increase->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_PLUS), wxASCII_STR(wxART_MENU) ), wxNullBitmap );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_view_font_increase->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_PLUS), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_view->Append( m_view_font_increase );

	wxMenuItem* m_view_font_decrease;
	m_view_font_decrease = new wxMenuItem( m_menu_view, ID_FONT_DECREASE, wxString( _("Decrease font") ) + wxT('\t') + wxT("CTRL+-"), _("Decrease font size, Ctrl+Wheel "), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_view_font_decrease->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_MINUS), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_view_font_decrease->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_MINUS), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_view->Append( m_view_font_decrease );

	wxMenuItem* m_view_font_default;
	m_view_font_default = new wxMenuItem( m_menu_view, ID_FONT_DEFAULT, wxString( _("Default font") ) + wxT('\t') + wxT("CTRL+0"), _("Set default font size"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_view_font_default->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_UNDO), wxASCII_STR(wxART_MENU) ), wxNullBitmap );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_view_font_default->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_UNDO), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_view->Append( m_view_font_default );

	m_menu_view->AppendSeparator();

	wxMenuItem* m_view_reset;
	m_view_reset = new wxMenuItem( m_menu_view, wxID_ANY, wxString( _("Reset settings") ) , _("Reset all settings"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_view_reset->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_CLOSE), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_view_reset->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_CLOSE), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_view->Append( m_view_reset );

	m_menubar->Append( m_menu_view, _("View") );

	m_menu_devices = new wxMenu();
	wxMenuItem* m_cmd_reload;
	m_cmd_reload = new wxMenuItem( m_menu_devices, wxID_REFRESH, wxString( _("Reload") ) + wxT('\t') + wxT("CTRL+R"), _("Show attached devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_reload->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_REDO), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_reload->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_REDO), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_devices->Append( m_cmd_reload );

	wxMenuItem* m_cmd_add;
	m_cmd_add = new wxMenuItem( m_menu_devices, wxID_ANY, wxString( _("A&dd") ) + wxT('\t') + wxT("CTRL+I"), _("Add remote devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_add->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_PLUS), wxASCII_STR(wxART_MENU) ), wxNullBitmap );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_add->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_PLUS), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_devices->Append( m_cmd_add );

	m_menu_devices->AppendSeparator();

	wxMenuItem* m_cmd_attach;
	m_cmd_attach = new wxMenuItem( m_menu_devices, wxID_ANY, wxString( _("Attach") ) + wxT('\t') + wxT("CTRL+T"), _("Attach selected devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_attach->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_TICK_MARK), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_attach->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_TICK_MARK), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_devices->Append( m_cmd_attach );

	wxMenuItem* m_cmd_detach;
	m_cmd_detach = new wxMenuItem( m_menu_devices, wxID_ANY, wxString( _("Detach") ) + wxT('\t') + wxT("CTRL+D"), _("Detach selected devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_detach->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_CROSS_MARK), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_detach->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_CROSS_MARK), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_devices->Append( m_cmd_detach );

	m_menu_devices->AppendSeparator();

	wxMenuItem* m_cmd_detach_all;
	m_cmd_detach_all = new wxMenuItem( m_menu_devices, wxID_ANY, wxString( _("Detach &All") ) + wxT('\t') + wxT("CTRL+X"), _("Detach all devices"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_cmd_detach_all->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_DELETE), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_cmd_detach_all->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_DELETE), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_devices->Append( m_cmd_detach_all );

	m_menubar->Append( m_menu_devices, _("Devices") );

	m_menu_log = new wxMenu();
	wxMenuItem* m_log_toggle;
	m_log_toggle = new wxMenuItem( m_menu_log, ID_LOG_TOGGLE, wxString( _("Toggle window") ) + wxT('\t') + wxT("CTRL+W"), _("Show/hide the window with log records"), wxITEM_CHECK );
	#ifdef __WXMSW__
	m_log_toggle->SetBitmaps( wxNullBitmap );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_log_toggle->SetBitmap( wxNullBitmap );
	#endif
	m_menu_log->Append( m_log_toggle );

	wxMenuItem* m_log_verbose;
	m_log_verbose = new wxMenuItem( m_menu_log, wxID_ANY, wxString( _("Verbose") ) , _("Show debug messages in the log window"), wxITEM_CHECK );
	#ifdef __WXMSW__
	m_log_verbose->SetBitmaps( wxNullBitmap );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_log_verbose->SetBitmap( wxNullBitmap );
	#endif
	m_menu_log->Append( m_log_verbose );

	m_menubar->Append( m_menu_log, _("Log") );

	m_menu_log_help = new wxMenu();
	wxMenuItem* m_help_about;
	m_help_about = new wxMenuItem( m_menu_log_help, wxID_ABOUT, wxString( _("About") ) , wxEmptyString, wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_help_about->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_TIP), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_help_about->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_TIP), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_log_help->Append( m_help_about );

	wxMenuItem* m_help_about_lib;
	m_help_about_lib = new wxMenuItem( m_menu_log_help, wxID_ANY, wxString( _("wxWidgets") ) , _("Show information about the library"), wxITEM_NORMAL );
	#ifdef __WXMSW__
	m_help_about_lib->SetBitmaps( wxArtProvider::GetBitmap( wxASCII_STR(wxART_INFORMATION), wxASCII_STR(wxART_MENU) ) );
	#elif (defined( __WXGTK__ ) || defined( __WXOSX__ ))
	m_help_about_lib->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_INFORMATION), wxASCII_STR(wxART_MENU) ) );
	#endif
	m_menu_log_help->Append( m_help_about_lib );

	m_menubar->Append( m_menu_log_help, _("Help") );

	this->SetMenuBar( m_menubar );

	m_auiToolBar = new wxAuiToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_DEFAULT_STYLE|wxAUI_TB_GRIPPER|wxAUI_TB_TEXT );
	m_auiToolBar->SetToolSeparation( 1 );
	m_tool_reload = m_auiToolBar->AddTool( wxID_REFRESH, _("Reload"), wxArtProvider::GetBitmap( wxASCII_STR(wxART_REDO), wxASCII_STR(wxART_TOOLBAR) ), wxNullBitmap, wxITEM_NORMAL, _("Show attached devices"), wxEmptyString, NULL );

	m_auiToolBar->AddSeparator();

	m_tool_attach = m_auiToolBar->AddTool( wxID_ANY, _("Attach"), wxArtProvider::GetBitmap( wxASCII_STR(wxART_TICK_MARK), wxASCII_STR(wxART_TOOLBAR) ), wxNullBitmap, wxITEM_NORMAL, _("Attach selected devices"), wxEmptyString, NULL );

	m_tool_detach = m_auiToolBar->AddTool( wxID_ANY, _("Detach"), wxArtProvider::GetBitmap( wxASCII_STR(wxART_CROSS_MARK), wxASCII_STR(wxART_TOOLBAR) ), wxNullBitmap, wxITEM_NORMAL, _("Detach selected devices"), wxEmptyString, NULL );

	m_tool_detach_all = m_auiToolBar->AddTool( wxID_ANY, _("Detach All"), wxArtProvider::GetBitmap( wxASCII_STR(wxART_DELETE), wxASCII_STR(wxART_TOOLBAR) ), wxNullBitmap, wxITEM_NORMAL, _("Detach all remote devices"), wxEmptyString, NULL );

	m_auiToolBar->AddSeparator();

	m_tool_load = m_auiToolBar->AddTool( wxID_ANY, _("Load"), wxArtProvider::GetBitmap( wxASCII_STR(wxART_PASTE), wxASCII_STR(wxART_TOOLBAR) ), wxNullBitmap, wxITEM_NORMAL, _("Load saved devices"), wxEmptyString, NULL );

	m_tool_save = m_auiToolBar->AddTool( wxID_SAVE, _("Save"), wxArtProvider::GetBitmap( wxASCII_STR(wxART_FILE_SAVE), wxASCII_STR(wxART_TOOLBAR) ), wxNullBitmap, wxITEM_NORMAL, _("Save devices"), wxEmptyString, NULL );

	m_auiToolBar->Realize();
	m_mgr.AddPane( m_auiToolBar, wxAuiPaneInfo() .Top() .CaptionVisible( false ).CloseButton( false ).Gripper().Dock().Resizable().FloatingSize( wxSize( -1,-1 ) ).Row( 0 ).Layer( 10 ).ToolbarPane() );

	m_auiToolBarAdd = new wxAuiToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_DEFAULT_STYLE|wxAUI_TB_GRIPPER|wxTAB_TRAVERSAL );
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
	m_button_add = new wxButton( m_auiToolBarAdd, wxID_ADD, _("Add devices"), wxDefaultPosition, wxDefaultSize, 0 );

	m_button_add->SetDefault();

	m_button_add->SetBitmap( wxArtProvider::GetBitmap( wxASCII_STR(wxART_PLUS), wxASCII_STR(wxART_BUTTON) ) );
	m_auiToolBarAdd->AddControl( m_button_add );
	m_auiToolBarAdd->Realize();
	m_mgr.AddPane( m_auiToolBarAdd, wxAuiPaneInfo() .Top() .CaptionVisible( false ).CloseButton( false ).Gripper().Dock().Resizable().FloatingSize( wxSize( 137,137 ) ).Row( 0 ).Layer( 10 ).ToolbarPane() );

	m_treeListCtrl = new wxTreeListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTL_MULTIPLE );
	m_treeListCtrl->SetFont( wxFont( wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString ) );
	m_treeListCtrl->SetBackgroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW ) );

	m_mgr.AddPane( m_treeListCtrl, wxAuiPaneInfo() .Center() .Caption( _("USB devices") ).CloseButton( false ).PaneBorder( false ).Movable( false ).Dock().Resizable().FloatingSize( wxDefaultSize ).DockFixed( true ).BottomDockable( false ).TopDockable( false ).LeftDockable( false ).RightDockable( false ).Floatable( false ) );

	m_treeListCtrl->AppendColumn( _("Server / Bus-Id"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Port"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_CENTER, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Speed"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Vendor"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Product"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("State"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Auto"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_CENTER, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Notes"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_REORDERABLE|wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Saved State"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_HIDDEN );


	m_mgr.Update();
	this->Centre( wxBOTH );

	// Connect Events
	this->Connect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( Frame::on_close ) );
	this->Connect( wxEVT_MOUSEWHEEL, wxMouseEventHandler( Frame::on_frame_mouse_wheel ) );
	m_menu_file->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_load ), this, m_file_load->GetId());
	m_menu_file->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_save ), this, m_file_save->GetId());
	m_menu_file->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_save_selected ), this, m_file_save_selected->GetId());
	this->Connect( m_file_save_selected->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	m_menu_file->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_exit ), this, m_file_exit->GetId());
	m_menu_edit->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_select_all ), this, m_select_all->GetId());
	this->Connect( m_select_all->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_devices_update_ui ) );
	m_menu_edit->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_toogle_auto ), this, m_toggle_auto->GetId());
	this->Connect( m_toggle_auto->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	m_menu_edit->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_edit_notes ), this, m_edit_notes->GetId());
	this->Connect( m_edit_notes->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_edit_notes_update_ui ) );
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
	m_menu_columns->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_column ), this, m_view_notes->GetId());
	this->Connect( m_view_notes->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	m_menu_view->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_zebra ), this, m_view_zebra->GetId());
	this->Connect( m_view_zebra->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_zabra_update_ui ) );
	m_menu_view->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_labels ), this, m_view_labels->GetId());
	this->Connect( m_view_labels->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_labels_update_ui ) );
	m_menu_view->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_font_increase ), this, m_view_font_increase->GetId());
	m_menu_view->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_font_decrease ), this, m_view_font_decrease->GetId());
	m_menu_view->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_font_default ), this, m_view_font_default->GetId());
	m_menu_view->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_view_reset ), this, m_view_reset->GetId());
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_reload ), this, m_cmd_reload->GetId());
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::add_exported_devices ), this, m_cmd_add->GetId());
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_attach ), this, m_cmd_attach->GetId());
	this->Connect( m_cmd_attach->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_detach ), this, m_cmd_detach->GetId());
	this->Connect( m_cmd_detach->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	m_menu_devices->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_detach_all ), this, m_cmd_detach_all->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_show ), this, m_log_toggle->GetId());
	this->Connect( m_log_toggle->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_show_update_ui ) );
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_verbose ), this, m_log_verbose->GetId());
	this->Connect( m_log_verbose->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_verbose_update_ui ) );
	m_menu_log_help->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_help_about ), this, m_help_about->GetId());
	m_menu_log_help->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_help_about_lib ), this, m_help_about_lib->GetId());
	m_auiToolBar->Connect( wxEVT_MOUSEWHEEL, wxMouseEventHandler( Frame::on_frame_mouse_wheel ), NULL, this );
	this->Connect( m_tool_reload->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_reload ) );
	this->Connect( m_tool_attach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_attach ) );
	this->Connect( m_tool_attach->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	this->Connect( m_tool_detach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach ) );
	this->Connect( m_tool_detach->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	this->Connect( m_tool_detach_all->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach_all ) );
	this->Connect( m_tool_load->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_load ) );
	this->Connect( m_tool_save->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_save ) );
	m_auiToolBarAdd->Connect( wxEVT_MOUSEWHEEL, wxMouseEventHandler( Frame::on_frame_mouse_wheel ), NULL, this );
	m_button_add->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( Frame::add_exported_devices ), NULL, this );
	m_treeListCtrl->Connect( wxEVT_TREELIST_ITEM_ACTIVATED, wxTreeListEventHandler( Frame::on_item_activated ), NULL, this );
	m_treeListCtrl->Connect( wxEVT_TREELIST_ITEM_CONTEXT_MENU, wxTreeListEventHandler( Frame::on_item_context_menu ), NULL, this );
}

Frame::~Frame()
{
	// Disconnect Events
	this->Disconnect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( Frame::on_close ) );
	this->Disconnect( wxEVT_MOUSEWHEEL, wxMouseEventHandler( Frame::on_frame_mouse_wheel ) );
	this->Disconnect( wxID_SAVEAS, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	this->Disconnect( wxID_SELECTALL, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_devices_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_edit_notes_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_column_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_zabra_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_view_labels_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	this->Disconnect( ID_LOG_TOGGLE, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_show_update_ui ) );
	this->Disconnect( wxID_ANY, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_verbose_update_ui ) );
	m_auiToolBar->Disconnect( wxEVT_MOUSEWHEEL, wxMouseEventHandler( Frame::on_frame_mouse_wheel ), NULL, this );
	this->Disconnect( m_tool_reload->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_reload ) );
	this->Disconnect( m_tool_attach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_attach ) );
	this->Disconnect( m_tool_attach->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	this->Disconnect( m_tool_detach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach ) );
	this->Disconnect( m_tool_detach->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_has_selected_devices_update_ui ) );
	this->Disconnect( m_tool_detach_all->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach_all ) );
	this->Disconnect( m_tool_load->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_load ) );
	this->Disconnect( m_tool_save->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_save ) );
	m_auiToolBarAdd->Disconnect( wxEVT_MOUSEWHEEL, wxMouseEventHandler( Frame::on_frame_mouse_wheel ), NULL, this );
	m_button_add->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( Frame::add_exported_devices ), NULL, this );
	m_treeListCtrl->Disconnect( wxEVT_TREELIST_ITEM_ACTIVATED, wxTreeListEventHandler( Frame::on_item_activated ), NULL, this );
	m_treeListCtrl->Disconnect( wxEVT_TREELIST_ITEM_CONTEXT_MENU, wxTreeListEventHandler( Frame::on_item_context_menu ), NULL, this );

	m_mgr.UnInit();

}
