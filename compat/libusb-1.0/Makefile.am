AUTOMAKE_OPTIONS = dist-bzip2 no-dist-gzip
ACLOCAL_AMFLAGS = -I m4
DISTCLEANFILES = libusb-1.0.pc
EXTRA_DIST = TODO PORTING libusb/libusb-1.0.def libusb/version_nano.h \
  android Xcode
SUBDIRS = libusb

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA=libusb-1.0.pc

.PHONY: dist-up

reldir = .release/$(distdir)
dist-up: dist
	rm -rf $(reldir)
	mkdir -p $(reldir)
	cp $(distdir).tar.bz2 $(reldir)
	rsync -rv $(reldir) frs.sourceforge.net:/home/frs/project/l/li/libusb/libusb-1.0/
	rm -rf $(reldir)
