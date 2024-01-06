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
	wxMenuItem* m_cmd_list;
	m_cmd_list = new wxMenuItem( m_menu_commands, wxID_ANY, wxString( _("List") ) , wxEmptyString, wxITEM_NORMAL );
	m_menu_commands->Append( m_cmd_list );

	wxMenuItem* m_cmd_attach;
	m_cmd_attach = new wxMenuItem( m_menu_commands, wxID_ANY, wxString( _("Attach") ) , wxEmptyString, wxITEM_NORMAL );
	m_menu_commands->Append( m_cmd_attach );

	wxMenuItem* m_cmd_detach;
	m_cmd_detach = new wxMenuItem( m_menu_commands, wxID_ANY, wxString( _("Detach") ) , wxEmptyString, wxITEM_NORMAL );
	m_menu_commands->Append( m_cmd_detach );

	wxMenuItem* m_cmd_refresh;
	m_cmd_refresh = new wxMenuItem( m_menu_commands, ID_CMD_REFRESH, wxString( _("Refresh") ) , wxEmptyString, wxITEM_NORMAL );
	m_menu_commands->Append( m_cmd_refresh );

	m_menubar->Append( m_menu_commands, _("Commands") );

	m_menu_log = new wxMenu();
	wxMenuItem* m_log_show;
	m_log_show = new wxMenuItem( m_menu_log, ID_LOG_SHOW, wxString( _("Show") ) , _("Show window with log records"), wxITEM_CHECK );
	m_menu_log->Append( m_log_show );

	m_menu_log->AppendSeparator();

	wxMenuItem* m_log_level_error;
	m_log_level_error = new wxMenuItem( m_menu_log, ID_LOG_LEVEL_ERROR, wxString( _("Error") ) , _("A serious error, user must be informed about it"), wxITEM_RADIO );
	m_menu_log->Append( m_log_level_error );

	wxMenuItem* m_log_level_warning;
	m_log_level_warning = new wxMenuItem( m_menu_log, ID_LOG_LEVEL_WARNING, wxString( _("Warning") ) , _("User is normally informed about it but may be ignored"), wxITEM_RADIO );
	m_menu_log->Append( m_log_level_warning );

	wxMenuItem* m_log_level_message;
	m_log_level_message = new wxMenuItem( m_menu_log, ID_LOG_LEVEL_MESSAGE, wxString( _("Message") ) , _("Normal message"), wxITEM_RADIO );
	m_menu_log->Append( m_log_level_message );

	wxMenuItem* m_log_level_status;
	m_log_level_status = new wxMenuItem( m_menu_log, ID_LOG_LEVEL_STATUS, wxString( _("Status") ) , _("Informational: might go to the status bar"), wxITEM_RADIO );
	m_menu_log->Append( m_log_level_status );

	wxMenuItem* m_log_level_info;
	m_log_level_info = new wxMenuItem( m_menu_log, ID_LOG_LEVEL_INFO, wxString( _("Info") ) , _("Informational message (a.k.a. 'Verbose')"), wxITEM_RADIO );
	m_menu_log->Append( m_log_level_info );

	m_menubar->Append( m_menu_log, _("Log") );

	this->SetMenuBar( m_menubar );

	m_auiToolBar = new wxAuiToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT );
	m_toolPort = m_auiToolBar->AddTool( wxID_ANY, _("Port"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL );

	m_toolAttach = m_auiToolBar->AddTool( wxID_ANY, _("Attach"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL );

	m_toolDetach = m_auiToolBar->AddTool( wxID_ANY, _("Detach"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL );

	m_auiToolBar->Realize();
	m_mgr.AddPane( m_auiToolBar, wxAuiPaneInfo() .Left() .PinButton( true ).Dock().Resizable().FloatingSize( wxDefaultSize ) );

	m_treeListCtrl = new wxTreeListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTL_DEFAULT_STYLE );
	m_mgr.AddPane( m_treeListCtrl, wxAuiPaneInfo() .Center() .PinButton( true ).Dock().Resizable().FloatingSize( wxDefaultSize ) );

	m_treeListCtrl->AppendColumn( _("Server / Bus-Id"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Port"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_RIGHT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("USB Speed"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Vendor"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("Product"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );
	m_treeListCtrl->AppendColumn( _("State"), wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT, wxCOL_RESIZABLE|wxCOL_SORTABLE );


	m_mgr.Update();
	this->Centre( wxBOTH );

	// Connect Events
	this->Connect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( Frame::on_close ) );
	m_menu_file->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_exit ), this, m_file_exit->GetId());
	m_menu_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_list ), this, m_cmd_list->GetId());
	m_menu_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_attach ), this, m_cmd_attach->GetId());
	m_menu_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_detach ), this, m_cmd_detach->GetId());
	m_menu_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_refresh ), this, m_cmd_refresh->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_show ), this, m_log_show->GetId());
	this->Connect( m_log_show->GetId(), wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_show_update_ui ) );
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_log_level_error->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_log_level_warning->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_log_level_message->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_log_level_status->GetId());
	m_menu_log->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_log_level ), this, m_log_level_info->GetId());
	this->Connect( m_toolPort->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_port ) );
	this->Connect( m_toolAttach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_attach ) );
	this->Connect( m_toolDetach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach ) );
}

Frame::~Frame()
{
	// Disconnect Events
	this->Disconnect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( Frame::on_close ) );
	this->Disconnect( ID_LOG_SHOW, wxEVT_UPDATE_UI, wxUpdateUIEventHandler( Frame::on_log_show_update_ui ) );
	this->Disconnect( m_toolPort->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_port ) );
	this->Disconnect( m_toolAttach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_attach ) );
	this->Disconnect( m_toolDetach->GetId(), wxEVT_COMMAND_TOOL_CLICKED, wxCommandEventHandler( Frame::on_detach ) );

	m_mgr.UnInit();

}
