dist_gq_bin_SCRIPTS = geeqie-symlink

gq_desktopdir = $(appdir)/applications
gq_desktop_in_files = symlink.desktop.in
gq_desktop_DATA = $(gq_desktop_in_files:.desktop.in=.desktop)
SUFFIXES = .desktop.in .desktop
.desktop.in.desktop:
	$(MSGFMT) --desktop --template=$< -d $(top_srcdir)/po -o $@

EXTRA_DIST = $(gq_desktop_in_files)
CLEANFILES = $(gq_desktop_DATA)
