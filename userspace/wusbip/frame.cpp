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
	wxMenuItem* m_menuItemExit;
	m_menuItemExit = new wxMenuItem( m_menu_file, wxID_ANY, wxString( _("Exit") ) , wxEmptyString, wxITEM_NORMAL );
	m_menu_file->Append( m_menuItemExit );

	m_menubar->Append( m_menu_file, _("File") );

	m_menubar_commands = new wxMenu();
	wxMenuItem* m_menuItemList;
	m_menuItemList = new wxMenuItem( m_menubar_commands, wxID_ANY, wxString( _("List") ) , wxEmptyString, wxITEM_NORMAL );
	m_menubar_commands->Append( m_menuItemList );

	wxMenuItem* m_menuItemAttach;
	m_menuItemAttach = new wxMenuItem( m_menubar_commands, wxID_ANY, wxString( _("Attach") ) , wxEmptyString, wxITEM_NORMAL );
	m_menubar_commands->Append( m_menuItemAttach );

	wxMenuItem* m_menuItemDetach;
	m_menuItemDetach = new wxMenuItem( m_menubar_commands, wxID_ANY, wxString( _("Detach") ) , wxEmptyString, wxITEM_NORMAL );
	m_menubar_commands->Append( m_menuItemDetach );

	wxMenuItem* m_menuItemPort;
	m_menuItemPort = new wxMenuItem( m_menubar_commands, wxID_ANY, wxString( _("Port") ) , wxEmptyString, wxITEM_NORMAL );
	m_menubar_commands->Append( m_menuItemPort );

	m_menubar->Append( m_menubar_commands, _("Commands") );

	this->SetMenuBar( m_menubar );


	m_mgr.Update();
	this->Centre( wxBOTH );

	// Connect Events
	m_menu_file->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_exit ), this, m_menuItemExit->GetId());
	m_menubar_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_list ), this, m_menuItemList->GetId());
	m_menubar_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_attach ), this, m_menuItemAttach->GetId());
	m_menubar_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_detach ), this, m_menuItemDetach->GetId());
	m_menubar_commands->Bind(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler( Frame::on_port ), this, m_menuItemPort->GetId());
}

Frame::~Frame()
{
	// Disconnect Events

	m_mgr.UnInit();

}
