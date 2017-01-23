
ACLOCAL_AMFLAGS = -I m4

JANSSON_CPPFLAGS= -I$(top_builddir)/compat/jansson-2.9/src -I$(top_srcdir)/compat/jansson-2.9/src

if WANT_USBUTILS
USBUTILS_CPPFLAGS = `pkg-config libusb-1.0 --libs --cflags`
else
USBUTILS_CPPFLAGS =
endif

if STATIC_CURL
LIBCURL_CPPFLAGS = -DCURL_STATICLIB
else
LIBCURL_CPPFLAGS =
endif

EXTRA_DIST	= example.conf linux-usb-cgminer \
		  api-example.php miner.php	\
		  API.class API.java api-example.c windows-build.txt \
		  bitstreams/README API-README FPGA-README \
		  bitforce-firmware-flash.c hexdump.c ASIC-README \
		  01-cgminer.rules

SUBDIRS		= lib compat ccan

cgminer_CPPFLAGS = $(PTHREAD_FLAGS) -fno-strict-aliasing $(JANSSON_CPPFLAGS) $(USBUTILS_CPPFLAGS) $(LIBCURL_CPPFLAGS)

bin_PROGRAMS	= cgminer

cgminer_LDFLAGS	= $(PTHREAD_FLAGS)
cgminer_LDADD	= $(DLOPEN_FLAGS) @LIBCURL_LIBS@ @JANSSON_LIBS@ @PTHREAD_LIBS@ \
		  @NCURSES_LIBS@ @PDCURSES_LIBS@ @WS2_LIBS@ \
		  @LIBUSB_LIBS@ @MM_LIBS@ @RT_LIBS@ @LIBSYSTEMD_LIBS@ \
		  @MATH_LIBS@ lib/libgnu.a ccan/libccan.a

cgminer_CPPFLAGS += -I$(top_builddir)/lib -I$(top_srcdir)/lib

if !HAVE_WINDOWS
cgminer_CPPFLAGS += @LIBCURL_CFLAGS@
endif

# common sources
cgminer_SOURCES := cgminer.c

cgminer_SOURCES	+= elist.h miner.h compat.h bench_block.h	\
		   util.c util.h uthash.h logging.h		\
		   sha2.c sha2.h api.c

cgminer_SOURCES	+= logging.c

cgminer_SOURCES	+= klist.h klist.c

cgminer_SOURCES	+= noncedup.c

if NEED_FPGAUTILS
cgminer_SOURCES += fpgautils.c fpgautils.h
endif

if WANT_USBUTILS
cgminer_SOURCES += usbutils.c usbutils.h
endif

if WANT_LIBBITFURY
cgminer_SOURCES += libbitfury.c libbitfury.h mcp2210.c mcp2210.h
endif

if WANT_CRC16
cgminer_SOURCES += crc16.c crc.h
endif

# Device drivers
if HAS_AVALON
cgminer_SOURCES += driver-avalon.c driver-avalon.h
endif

if HAS_KNC
cgminer_SOURCES += driver-knc.c knc-asic.c knc-asic.h knc-transport.h knc-transport-spi.c
cgminer_LDADD   += -lz
endif

if HAS_BFLSC
cgminer_SOURCES += driver-bflsc.c driver-bflsc.h
endif

if HAS_BITFORCE
cgminer_SOURCES += driver-bitforce.c
endif

if HAS_HASHFAST
cgminer_SOURCES += driver-hashfast.c driver-hashfast.h hf_protocol.h hf_protocol_be.h
endif

if HAS_HASHRATIO
cgminer_SOURCES += driver-hashratio.c driver-hashratio.h
endif

if HAS_BITFURY
cgminer_SOURCES += driver-bitfury.c driver-bitfury.h
endif

if HAS_BITMINE_A1
cgminer_SOURCES += driver-SPI-bitmine-A1.c
cgminer_SOURCES += spi-context.c spi-context.h
cgminer_SOURCES += A1-common.h
cgminer_SOURCES += A1-board-selector.h
cgminer_SOURCES += A1-board-selector-CCD.c A1-board-selector-CCR.c
cgminer_SOURCES += A1-trimpot-mcp4x.h A1-trimpot-mcp4x.c
cgminer_SOURCES += i2c-context.c i2c-context.h
endif

if HAS_DRILLBIT
cgminer_SOURCES += driver-drillbit.c driver-drillbit.h
endif

if HAS_ICARUS
cgminer_SOURCES += driver-icarus.c
endif

if HAS_KLONDIKE
cgminer_SOURCES += driver-klondike.c
endif

if HAS_COINTERRA
cgminer_SOURCES += driver-cointerra.c driver-cointerra.h
endif

if HAS_SP10
cgminer_SOURCES += driver-spondoolies-sp10.c driver-spondoolies-sp10.h \
		   driver-spondoolies-sp10-p.c driver-spondoolies-sp10-p.h
endif


if HAS_SP30
cgminer_SOURCES += driver-spondoolies-sp30.c driver-spondoolies-sp30.h \
		   driver-spondoolies-sp30-p.c driver-spondoolies-sp30-p.h
endif

if HAS_BAB
cgminer_SOURCES += driver-bab.c
endif

if HAS_AVALON2
cgminer_SOURCES += driver-avalon2.c driver-avalon2.h
endif

if HAS_AVALON4
cgminer_SOURCES += driver-avalon4.c driver-avalon4.h
endif

if HAS_AVALON7
cgminer_SOURCES += driver-avalon7.c driver-avalon7.h
endif

if NEED_I2C_CONTEXT
cgminer_SOURCES += i2c-context.c
endif

if HAS_AVALON_MINER
cgminer_SOURCES += driver-avalon-miner.c driver-avalon-miner.h
endif

if HAS_MINION
cgminer_SOURCES += driver-minion.c
endif

if HAS_ANT_S1
cgminer_SOURCES += driver-bitmain.c driver-bitmain.h
endif

if HAS_ANT_S2
cgminer_SOURCES += driver-bitmain.c driver-bitmain.h
endif

if HAS_ANT_S3
cgminer_SOURCES += driver-bitmain.c driver-bitmain.h
endif

if HAS_MODMINER
cgminer_SOURCES += driver-modminer.c
bitstreamsdir = $(bindir)/bitstreams
dist_bitstreams_DATA = $(top_srcdir)/bitstreams/README
endif

if HAS_BLOCKERUPTER
cgminer_SOURCES += driver-blockerupter.c driver-blockerupter.h
endif
