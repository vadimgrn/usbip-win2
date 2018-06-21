#include "stubctl.h"

#include "usbip_stub.h"

BOOL install_driver_service(void);
BOOL uninstall_driver_service(void);

static BOOL
is_admin(void)
{
	SID_IDENTIFIER_AUTHORITY	NtAuthority = { SECURITY_NT_AUTHORITY };
	PSID	AdministratorsGroup;
	BOOL	isMember;

	if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
				      DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdministratorsGroup))
		return FALSE;

	if (!CheckTokenMembership(NULL, AdministratorsGroup, &isMember)) {
		isMember = FALSE;
	}
	FreeSid(AdministratorsGroup);

	return isMember;
}

int
install_stub_driver(devno_t devno)
{
	if (!is_admin()) {
		err("requires administrative privileges.\n");
		return 1;
	}

	if (devno == 0) {
		install_driver_service();
	}
	else {
		if (attach_stub_driver(devno)) {
			info("filter inserted successfully: %hhu", devno);
		}
		else {
			info("failed to insert filter: %hhu", devno);
		}
	}

	return 0;
}

int
uninstall_stub_driver(devno_t devno)
{
	if (devno == 0)
		uninstall_driver_service();
	else
		detach_stub_driver(devno);

	return 0;
}
