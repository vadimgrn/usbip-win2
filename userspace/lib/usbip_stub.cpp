#include "usbip_setupdi.h"
#include "usbip_stub.h"
#include "usbip_util.h"
#include "usbip_common.h"

#include <stdlib.h>
#include <stdio.h>
#include <newdev.h>

char *get_dev_property(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD prop);

BOOL build_cat(const char *path, const char *catname, const char *hwid);
int sign_file(LPCSTR subject, LPCSTR fpath);

BOOL
is_service_usbip_stub(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data)
{
	char	*svcname;
	BOOL	res;

	svcname = get_dev_property(dev_info, dev_info_data, SPDRP_SERVICE);
	if (svcname == nullptr)
		return FALSE;
	res = _stricmp(svcname, STUB_DRIVER_SVCNAME) == 0 ? TRUE: FALSE;
	free(svcname);
	return res;
}

static void
copy_file(const char *fname, const char *path_drvpkg)
{
	auto path = get_module_dir();
	if (path.empty()) {
		return;
	}
        
        auto src = path + '\\' + fname;
        auto dst = std::string(path_drvpkg) + '\\' + fname;

	CopyFile(src.c_str(), dst.c_str(), TRUE);
}

static void
translate_inf(const char *id_hw, FILE *in, FILE *out)
{
	char	buf[4096];
	char	*line;

	while ((line = fgets(buf, 4096, in))) {
		char	*mark;

		mark = strstr(line, "%hwid%");
		if (mark) {
			strcpy_s(mark, 4096 - (mark - buf), id_hw);
		}
		fwrite(line, strlen(line), 1, out);
	}
}

static void
copy_stub_inf(const char *id_hw, const char *path_drvpkg)
{
	auto path_mod = get_module_dir();
        if (path_mod.empty()) {
                return;
        }

	auto path_inx = path_mod + "\\usbip_stub.inx";

        FILE *in{};
        if (fopen_s(&in, path_inx.c_str(), "r")) {
		return;
	}

        auto path_dst = std::string(path_drvpkg) + "\\usbip_stub.inf";

        FILE *out{};
	if (fopen_s(&out, path_dst.c_str(), "w")) {
		fclose(in);
		return;
	}

	translate_inf(id_hw, in, out);
	fclose(in);
	fclose(out);
}

static void
remove_dir_all(const char *path_dir)
{
        std::string dirr = path_dir;
        WIN32_FIND_DATA	wfd;

        auto fpath = dirr + "\\*";
	auto hfs = FindFirstFile(fpath.c_str(), &wfd);

        if (hfs != INVALID_HANDLE_VALUE) {
		do {
			if (*wfd.cFileName != '.') {
				fpath = dirr + '\\' + wfd.cFileName;
                                DeleteFile(fpath.c_str());
			}
		} while (FindNextFile(hfs, &wfd));

		FindClose(hfs);
	}
	RemoveDirectory(path_dir);
}

static BOOL
get_temp_drvpkg_path(char path_drvpkg[])
{
	char tempdir[MAX_PATH + 1];

	if (GetTempPath(MAX_PATH + 1, tempdir) == 0)
		return FALSE;
	if (GetTempFileName(tempdir, "stub", 0, path_drvpkg) > 0) {
		DeleteFile(path_drvpkg);
		if (CreateDirectory(path_drvpkg, nullptr))
			return TRUE;
	}
	else
		DeleteFile(path_drvpkg);
	return FALSE;
}

static int
apply_stub_fdo(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	path_drvpkg[MAX_PATH + 1];
	BOOL	reboot_required;

	auto id_hw = get_id_hw(dev_info, pdev_info_data);
	if (!id_hw) {
		return ERR_GENERAL;
        }

	if (!get_temp_drvpkg_path(path_drvpkg)) {
		free(id_hw);
		return ERR_GENERAL;
	}

	copy_file("usbip_stub.sys", path_drvpkg);
	copy_stub_inf(id_hw, path_drvpkg);

	if (!build_cat(path_drvpkg, "usbip_stub.cat", id_hw)) {
		remove_dir_all(path_drvpkg);
		free(id_hw);
		return ERR_GENERAL;
	}

        int ret = 0;
        auto path = std::string(path_drvpkg) + "\\usbip_stub.cat";

        if ((ret = sign_file("USBIP Test", path.c_str())) < 0) {
		remove_dir_all(path_drvpkg);
		free(id_hw);
		if (ret == ERR_NOTEXIST)
			return ERR_CERTIFICATE;
		return ERR_GENERAL;
	}

        /* update driver */
        path = std::string(path_drvpkg) + "\\usbip_stub.inf";

	if (!UpdateDriverForPlugAndPlayDevicesA(nullptr, id_hw, path.c_str(), INSTALLFLAG_NONINTERACTIVE | INSTALLFLAG_FORCE, &reboot_required)) {
		auto err = GetLastError();
		dbg("failed to update driver %s ; %s ; errorcode: 0x%lx", path.c_str(), id_hw, err);
		free(id_hw);
		remove_dir_all(path_drvpkg);
		if (err == 0xe0000242) {
			/* USBIP Test certificate is not installed at trusted publisher */
			return ERR_CERTIFICATE;
		}
		return ERR_GENERAL;
	}

        free(id_hw);
	remove_dir_all(path_drvpkg);

	return 0;
}

static BOOL
rollback_driver(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	BOOL	needReboot;

	if (!DiRollbackDriver(dev_info, pdev_info_data, nullptr, ROLLBACK_FLAG_NO_UI, &needReboot)) {
		dbg("failed to rollback driver: %lx", GetLastError());
		return FALSE;
	}
	return TRUE;
}

static int
walker_attach(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	devno_t	*pdevno = (devno_t *)ctx;

	if (devno == *pdevno) {
		int	ret = apply_stub_fdo(dev_info, pdev_info_data);
		if (ret < 0)
			return ret;
		return 1;
	}
	return 0;
}

int
attach_stub_driver(devno_t devno)
{
	int	ret;

	ret = traverse_usbdevs(walker_attach, TRUE, &devno);
	switch (ret) {
	case 0:
		return ERR_NOTEXIST;
	case 1:
		return 0;
	case ERR_CERTIFICATE:
		return ERR_CERTIFICATE;
	default:
		return ERR_GENERAL;
	}
}

static int
walker_detach(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	devno_t	*pdevno = (devno_t *)ctx;

	if (devno == *pdevno) {
		if (!rollback_driver(dev_info, pdev_info_data))
			return ERR_GENERAL;
		return 1;
	}
	return 0;
}

int
detach_stub_driver(devno_t devno)
{
	int	ret;

	ret = traverse_usbdevs(walker_detach, TRUE, &devno);
	if (ret == 1)
		return 0;
	if (ret == 0)
		return ERR_NOTEXIST;
	return ERR_GENERAL;
}
