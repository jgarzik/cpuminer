##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_maj], [4])
m4_define([v_min], [10])
m4_define([v_mic], [0])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_ver], [v_maj.v_min.v_mic])
m4_define([lt_rev], m4_eval(v_maj + v_min))
m4_define([lt_cur], v_mic)
m4_define([lt_age], v_min)
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##

AC_INIT([cgminer], [v_ver], [kernel@kolivas.org])

AC_PREREQ(2.59)
AC_CANONICAL_SYSTEM
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([cgminer.c])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([foreign subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_USE_SYSTEM_EXTENSIONS

##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_ifdef([v_rev], , [m4_define([v_rev], [0])])
m4_ifdef([v_rel], , [m4_define([v_rel], [])])
AC_DEFINE_UNQUOTED(CGMINER_MAJOR_VERSION, [v_maj], [Major version])
AC_DEFINE_UNQUOTED(CGMINER_MINOR_VERSION, [v_min], [Minor version])
AC_DEFINE_UNQUOTED(CGMINER_MINOR_SUBVERSION, [v_mic], [Micro version])
version_info="lt_rev:lt_cur:lt_age"
release_info="v_rel"
AC_SUBST(version_info)
AC_SUBST(release_info)
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
VMAJ=v_maj
AC_SUBST(VMAJ)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

dnl Make sure anyone changing configure.ac/Makefile.am has a clue
AM_MAINTAINER_MODE

dnl Checks for programs
AC_PROG_CC
gl_EARLY
AC_PROG_GCC_TRADITIONAL
AM_PROG_CC_C_O

# This breaks autoreconf so disable it
# LT_INIT([disable-shared])

gl_INIT

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(syslog.h)

AC_FUNC_ALLOCA

have_win32=false
PTHREAD_FLAGS="-lpthread"
DLOPEN_FLAGS="-ldl"
WS2_LIBS=""
MM_LIBS=""
MATH_LIBS="-lm"
RT_LIBS="-lrt"

case $target in
  amd64-*)
    have_x86_64=true
    ;;
  x86_64-*)
    have_x86_64=true
    ;;
  *)
    have_x86_64=false
    ;;
esac

case $target in
  *-*-linux-gnu*)
    have_linux=true
    ;;
  *-*-mingw*)
    have_win32=true
    PTHREAD_FLAGS=""
    DLOPEN_FLAGS=""
    WS2_LIBS="-lws2_32"
    MM_LIBS="-lwinmm"
    RT_LIBS=""
    #AC_DEFINE([_WIN32_WINNT], [0x0501], "WinNT version for XP+ support")
    ;;
  powerpc-*-darwin*)
    have_darwin=true
    CFLAGS="$CFLAGS -faltivec"
    PTHREAD_FLAGS=""
    RT_LIBS=""
    ;;
  *-*-darwin*)
    have_darwin=true
    PTHREAD_FLAGS=""
    RT_LIBS=""
	;;
  *-*-freebsd*)
    PTHREAD_FLAGS=""
    DLOPEN_FLAGS=""
    RT_LIBS=""
    ;;
esac

has_winpthread=false
if test "x$have_win32" = xtrue; then
        has_winpthread=true
        AC_CHECK_LIB(winpthread, nanosleep, , has_winpthread=false)
        PTHREAD_LIBS=-lwinpthread
fi

if test "x$has_winpthread" != xtrue; then
        AC_CHECK_LIB(pthread, pthread_create, ,
                AC_MSG_ERROR([Could not find pthread library - please install libpthread]))
        PTHREAD_LIBS=-lpthread
fi

# Drivers that are designed to be run on dedicated hardware should set standalone to yes
# All drivers should prepend an x to the drivercount

standalone="no"
drivercount=""

ants1="no"

AC_ARG_ENABLE([ants1],
	[AC_HELP_STRING([--enable-ants1],[Compile support for Antminer S1 Bitmain STANDALONE(default disabled)])],
	[ants1=$enableval]
	)
if test "x$ants1" = xyes; then
	AC_DEFINE([USE_ANT_S1], [1], [Defined to 1 if Antminer S1 Bitmain support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_ANT_S1], [test x$ants1 = xyes])

ants2="no"

AC_ARG_ENABLE([ants2],
	[AC_HELP_STRING([--enable-ants2],[Compile support for Antminer S2 Bitmain STANDALONE(default disabled)])],
	[ants2=$enableval]
	)
if test "x$ants2" = xyes; then
	AC_DEFINE([USE_ANT_S2], [1], [Defined to 1 if Antminer S2 Bitmain support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_ANT_S2], [test x$ants2 = xyes])

ants3="no"

AC_ARG_ENABLE([ants3],
	[AC_HELP_STRING([--enable-ants3],[Compile support for Antminer S3 Bitmain STANDALONE(default disabled)])],
	[ants3=$enableval]
	)
if test "x$ants3" = xyes; then
	AC_DEFINE([USE_ANT_S3], [1], [Defined to 1 if Antminer S3 Bitmain support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_ANT_S3], [test x$ants3 = xyes])

avalon="no"

AC_ARG_ENABLE([avalon],
	[AC_HELP_STRING([--enable-avalon],[Compile support for Avalon (default disabled)])],
	[avalon=$enableval]
	)
if test "x$avalon" = xyes; then
	AC_DEFINE([USE_AVALON], [1], [Defined to 1 if Avalon support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_AVALON], [test x$avalon = xyes])


avalon2="no"

AC_ARG_ENABLE([avalon2],
	[AC_HELP_STRING([--enable-avalon2],[Compile support for Avalon2 (default disabled)])],
	[avalon2=$enableval]
	)
if test "x$avalon2" = xyes; then
	AC_DEFINE([USE_AVALON2], [1], [Defined to 1 if Avalon2 support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_AVALON2], [test x$avalon2 = xyes])


avalon4="no"

AC_ARG_ENABLE([avalon4],
	[AC_HELP_STRING([--enable-avalon4],[Compile support for Avalon4 (default disabled)])],
	[avalon4=$enableval]
	)
if test "x$avalon4" = xyes; then
	AC_DEFINE([USE_AVALON4], [1], [Defined to 1 if Avalon4 support is wanted])
fi
AM_CONDITIONAL([HAS_AVALON4], [test x$avalon4 = xyes])

avalon7="no"

AC_ARG_ENABLE([avalon7],
	[AC_HELP_STRING([--enable-avalon7],[Compile support for Avalon7 (default disabled)])],
	[avalon7=$enableval]
	)
if test "x$avalon7" = xyes; then
	AC_DEFINE([USE_AVALON7], [1], [Defined to 1 if Avalon7 support is wanted])
fi
AM_CONDITIONAL([HAS_AVALON7], [test x$avalon7 = xyes])

avalon_miner="no"

AC_ARG_ENABLE([avalon_miner],
	[AC_HELP_STRING([--enable-avalon-miner],[Compile support for Avalon miner(default disabled)])],
	[avalon_miner=$enableval]
	)
if test "x$avalon_miner" = xyes; then
	AC_DEFINE([USE_AVALON_MINER], [1], [Defined to 1 if Avalon miner support is wanted])
fi
AM_CONDITIONAL([HAS_AVALON_MINER], [test x$avalon_miner = xyes])

bab="no"

AC_ARG_ENABLE([bab],
	[AC_HELP_STRING([--enable-bab],[Compile support for BlackArrow Bitfury STANDALONE(default disabled)])],
	[bab=$enableval]
	)
if test "x$bab" = xyes; then
	AC_DEFINE([USE_BAB], [1], [Defined to 1 if BlackArrow Bitfury support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_BAB], [test x$bab = xyes])


bflsc="no"

AC_ARG_ENABLE([bflsc],
	[AC_HELP_STRING([--enable-bflsc],[Compile support for BFL ASICs (default disabled)])],
	[bflsc=$enableval]
	)
if test "x$bflsc" = xyes; then
	AC_DEFINE([USE_BFLSC], [1], [Defined to 1 if BFL ASIC support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_BFLSC], [test x$bflsc = xyes])


bitforce="no"

AC_ARG_ENABLE([bitforce],
	[AC_HELP_STRING([--enable-bitforce],[Compile support for BitForce FPGAs (default disabled)])],
	[bitforce=$enableval]
	)
if test "x$bitforce" = xyes; then
	AC_DEFINE([USE_BITFORCE], [1], [Defined to 1 if BitForce support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_BITFORCE], [test x$bitforce = xyes])


bitfury="no"

AC_ARG_ENABLE([bitfury],
	[AC_HELP_STRING([--enable-bitfury],[Compile support for BitFury ASICs (default disabled)])],
	[bitfury=$enableval]
	)
if test "x$bitfury" = xyes; then
	AC_DEFINE([USE_BITFURY], [1], [Defined to 1 if BitFury ASIC support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_BITFURY], [test x$bitfury = xyes])


bitmine_A1="no"

AC_ARG_ENABLE([bitmine_A1],
	[AC_HELP_STRING([--enable-bitmine_A1],[Compile support for Bitmine.ch A1 ASICs STANDALONE(default disabled)])],
	[bitmine_A1=$enableval]
	)
if test "x$bitmine_A1" = xyes; then
	AC_DEFINE([USE_BITMINE_A1], [1], [Defined to 1 if Bitmine A1 support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_BITMINE_A1], [test x$bitmine_A1 = xyes])


blockerupter="no"

AC_ARG_ENABLE([blockerupter],
	[AC_HELP_STRING([--enable-blockerupter],[Compile support for BlockErupter BROKEN DRIVER (default disabled)])],
	[blockerupter=$enableval]
	)
if test "x$blockerupter" = xyes; then
	AC_DEFINE([USE_BLOCKERUPTER], [1], [Defined to 1 if BlockErupter support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_BLOCKERUPTER], [test x$blockerupter = xyes])


cointerra="no"

AC_ARG_ENABLE([cointerra],
	[AC_HELP_STRING([--enable-cointerra],[Compile support for Cointerra ASICs (default disabled)])],
	[cointerra=$enableval]
	)
if test "x$cointerra" = xyes; then
	AC_DEFINE([USE_COINTERRA], [1], [Defined to 1 if Cointerra support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_COINTERRA], [test x$cointerra = xyes])


drillbit="no"

AC_ARG_ENABLE([drillbit],
	[AC_HELP_STRING([--enable-drillbit],[Compile support for Drillbit BitFury ASICs (default disabled)])],
	[drillbit=$enableval]
	)
if test "x$drillbit" = xyes; then
	AC_DEFINE([USE_DRILLBIT], [1], [Defined to 1 if Drillbit BitFury support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_DRILLBIT], [test x$drillbit = xyes])


hashfast="no"

AC_ARG_ENABLE([hashfast],
	[AC_HELP_STRING([--enable-hashfast],[Compile support for Hashfast (default disabled)])],
	[hashfast=$enableval]
	)
if test "x$hashfast" = xyes; then
	AC_DEFINE([USE_HASHFAST], [1], [Defined to 1 if Hashfast support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_HASHFAST], [test x$hashfast = xyes])


hashratio="no"

AC_ARG_ENABLE([hashratio],
	[AC_HELP_STRING([--enable-hashratio],[Compile support for Hashratio (default disabled)])],
	[hashratio=$enableval]
	)
if test "x$hashratio" = xyes; then
	AC_DEFINE([USE_HASHRATIO], [1], [Defined to 1 if Hashratiosupport is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_HASHRATIO], [test x$hashratio = xyes])


icarus="no"

AC_ARG_ENABLE([icarus],
	[AC_HELP_STRING([--enable-icarus],[Compile support for Icarus (default disabled)])],
	[icarus=$enableval]
	)
if test "x$icarus" = xyes; then
	AC_DEFINE([USE_ICARUS], [1], [Defined to 1 if Icarus support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_ICARUS], [test x$icarus = xyes])


klondike="no"

AC_ARG_ENABLE([klondike],
	[AC_HELP_STRING([--enable-klondike],[Compile support for Klondike (default disabled)])],
	[klondike=$enableval]
	)
if test "x$klondike" = xyes; then
	AC_DEFINE([USE_KLONDIKE], [1], [Defined to 1 if Klondike support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_KLONDIKE], [test x$klondike = xyes])


knc="no"

AC_ARG_ENABLE([knc],
	[AC_HELP_STRING([--enable-knc],[Compile support for KnC miners STANDALONE(default disabled)])],
	[knc=$enableval]
	)
if test "x$knc" = xyes; then
	AC_DEFINE([USE_KNC], [1], [Defined to 1 if KnC miner support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_KNC], [test x$knc = xyes])


minion="no"

AC_ARG_ENABLE([minion],
	[AC_HELP_STRING([--enable-minion],[Compile support for Minion BlackArrow ASIC STANDALONE(default disabled)])],
	[minion=$enableval]
	)
if test "x$minion" = xyes; then
	AC_DEFINE([USE_MINION], [1], [Defined to 1 if Minion BlackArrow ASIC support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_MINION], [test x$minion = xyes])


modminer="no"

AC_ARG_ENABLE([modminer],
	[AC_HELP_STRING([--enable-modminer],[Compile support for ModMiner FPGAs(default disabled)])],
	[modminer=$enableval]
	)
if test "x$modminer" = xyes; then
	AC_DEFINE([USE_MODMINER], [1], [Defined to 1 if ModMiner support is wanted])
	drivercount=x$drivercount
fi
AM_CONDITIONAL([HAS_MODMINER], [test x$modminer = xyes])


sp10="no"

AC_ARG_ENABLE([sp10],
	[AC_HELP_STRING([--enable-sp10],[Compile support for Spondoolies SP10 STANDALONE(default disabled)])],
	[sp10=$enableval]
	)
if test "x$sp10" = xyes; then
	AC_DEFINE([USE_SP10], [1], [Defined to 1 if Spondoolies SP10 support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_SP10], [test x$sp10 = xyes])



sp30="no"

AC_ARG_ENABLE([sp30],
	[AC_HELP_STRING([--enable-sp30],[Compile support for Spondoolies SP30 STANDALONE(default disabled)])],
	[sp30=$enableval]
	)
if test "x$sp30" = xyes; then
	AC_DEFINE([USE_SP30], [1], [Defined to 1 if SP30 support is wanted])
	drivercount=x$drivercount
	standalone="yes"
fi
AM_CONDITIONAL([HAS_SP30], [test x$sp30 = xyes])


forcecombo="no"

AC_ARG_ENABLE([forcecombo],
	[AC_HELP_STRING([--enable-forcecombo],[Allow combinations of drivers not intended to be built together(default disabled)])],
	[forcecombo=$enableval]
	)
if test "x$forcecombo" = xyes; then
	standalone="no"
fi

curses="auto"

AC_ARG_WITH([curses],
	[AC_HELP_STRING([--without-curses],[Compile support for curses TUI (default enabled)])],
	[curses=$withval]
	)
if test "x$curses" = "xno"; then
	cursesmsg='User specified --without-curses. TUI support DISABLED'
else
	AC_SEARCH_LIBS(addstr, pdcurses ncurses pdcurses, [
		curses=yes
		cursesmsg="FOUND: ${ac_cv_search_addstr}"
		AC_DEFINE([HAVE_CURSES], [1], [Defined to 1 if curses TUI support is wanted])
	], [
		if test "x$curses" = "xyes"; then
			AC_MSG_ERROR([Could not find curses library - please install libncurses-dev or pdcurses-dev (or configure --without-curses)])
		else
			AC_MSG_WARN([Could not find curses library - if you want a TUI, install libncurses-dev or pdcurses-dev])
			curses=no
			cursesmsg='NOT FOUND. TUI support DISABLED'
		fi
	])
fi


#Add a new device to this list if it needs libusb, along with a no on the end.
if test x$avalon$avalon2$avalon4$avalon7$avalon_miner$bitforce$bitfury$blockerupter$modminer$bflsc$icarus$hashfast$hashratio$klondike$drillbit$cointerra$ants1$ants3 != xnononononononononononononononononono; then
	want_usbutils=true
else
	want_usbutils=false
fi

if test x$bitfury != xno; then
	want_libbitfury=true
else
	want_libbitfury=false
fi

if test x$avalon2$avalon4$avalon7$avalon_miner$hashratio != xnonononono; then
	want_crc16=true
else
	want_crc16=false
fi

AM_CONDITIONAL([NEED_FPGAUTILS], [test x$modminer != xno])
AM_CONDITIONAL([WANT_USBUTILS], [test x$want_usbutils != xfalse])
AM_CONDITIONAL([WANT_LIBBITFURY], [test x$want_libbitfury != xfalse])
AM_CONDITIONAL([HAVE_CURSES], [test x$curses = xyes])
AM_CONDITIONAL([HAVE_WINDOWS], [test x$have_win32 = xtrue])
AM_CONDITIONAL([HAVE_x86_64], [test x$have_x86_64 = xtrue])
AM_CONDITIONAL([WANT_CRC16], [test x$want_crc16 != xfalse])
AM_CONDITIONAL([NEED_I2C_CONTEXT], [test x$avalon4$avalon7 != xnono])

if test "x$want_usbutils" != xfalse; then
	AC_DEFINE([USE_USBUTILS], [1], [Defined to 1 if usbutils support required])
	case $target in
		*-*-freebsd*)
			LIBUSB_LIBS="-lusb"
			LIBUSB_CFLAGS=""
			AC_DEFINE(HAVE_LIBUSB, 1, [Define if you have libusb-1.0])
			;;
		*)
			PKG_CHECK_MODULES(LIBUSB, libusb-1.0, [AC_DEFINE(HAVE_LIBUSB, 1, [Define if you have libusb-1.0])], [AC_MSG_ERROR([Could not find usb library - please install libusb-1.0])])
			;;
	esac
else
	LIBUSB_LIBS=""
fi

djansson="no"
AC_ARG_WITH([system-jansson],
	[AC_HELP_STRING([--with-system-jansson],[NOT RECOMMENDED! Compile against dynamic system jansson. (Default use included static jansson)])],
	[djansson=$withval]
)

if test "x$djansson" != xno; then
	case $target in
		*-*-freebsd*)
			JANSSON_LIBS="-ljansson"
			JANSSON_CFLAGS=""
			AC_DEFINE(HAVE_JANSSON, 1, [Define if you have jansson >= 2.6])
			;;
		*)
			PKG_CHECK_MODULES(JANSSON, jansson >= 2.6, [AC_DEFINE(HAVE_JANSSON, 1, [Define if you have jansson >= 2.6])], [AC_MSG_ERROR([Could not find jansson library - please install jansson >= 2.6])])
			;;
	esac
else
	AC_CONFIG_SUBDIRS([compat/jansson-2.9])
	JANSSON_LIBS="compat/jansson-2.9/src/.libs/libjansson.a"
fi

AM_CONDITIONAL([WANT_STATIC_JANSSON], [test x$djansson = xno])

PKG_PROG_PKG_CONFIG()

if test "x$have_cgminer_sdk" = "xtrue"; then
	if test "x$have_x86_64" = xtrue; then
		ARCH_DIR=x86_64
	else
		ARCH_DIR=x86
	fi
	PKG_CONFIG="${PKG_CONFIG:-pkg-config} --define-variable=arch=$ARCH_DIR --define-variable=target=$target --define-variable=cgminersdkdir=$CGMINER_SDK"
	PKG_CONFIG_PATH="$CGMINER_SDK/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi

AC_SUBST(LIBUSB_LIBS)
AC_SUBST(LIBUSB_CFLAGS)

AC_ARG_ENABLE([libcurl],
	[AC_HELP_STRING([--disable-libcurl],[Disable building with libcurl for GBT support])],
	[libcurl=$enableval]
	)

static_curl="no"
if test "x$libcurl" != xno; then
	if test "x$have_win32" != xtrue; then
		PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.25.0], [AC_DEFINE([CURL_HAS_KEEPALIVE], [1], [Defined if version of curl supports keepalive.])],
			[PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.18.2], ,[AC_MSG_ERROR([Missing required libcurl dev >= 7.18.2])])])
	else
		PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.25.0], ,[AC_MSG_ERROR([Missing required libcurl dev >= 7.25.0])])
		AC_DEFINE([CURL_HAS_KEEPALIVE], [1])
		static_curl="yes"
	fi
	AC_DEFINE([HAVE_LIBCURL], [1], [Defined to 1 if libcurl support built in])
else
	LIBCURL_LIBS=""
fi
AC_SUBST(LIBCURL_LIBS)
AM_CONDITIONAL([STATIC_CURL], [test x$static_curl = xyes])

libsystemd="no"

AC_ARG_ENABLE([libsystemd],
	[AC_HELP_STRING([--enable-libsystemd],[Enable building with libsystemd for watchdog and status notification support])],
	[libsystemd=$enableval]
)

if test "x$libsystemd" != xno; then
	if test "x$have_linux" != xtrue; then
		AC_MSG_ERROR([libsystemd is only supported on Linux platforms])
	fi

	PKG_CHECK_MODULES(LIBSYSTEMD, libsystemd, , AC_MSG_ERROR(Could not find libsystemd dev))
	AC_DEFINE([USE_LIBSYSTEMD], [1], [Defined to 1 if libsystemd support is wanted])
else
	LIBSYSTEMD_LIBS=""
fi

#check execv signature
AC_COMPILE_IFELSE([AC_LANG_SOURCE([
		   #include <process.h>
		   int execv(const char*, const char*const*);
		   ])],
		   AC_DEFINE([EXECV_2ND_ARG_TYPE], [const char* const*], [int execv(const char*, const char*const*);]),
		   AC_DEFINE([EXECV_2ND_ARG_TYPE], [char* const*], [int execv(const char*, char*const*);]))

dnl CCAN wants to know a lot of vars.
# All the configuration checks.  Regrettably, the __attribute__ checks will
# give false positives on old GCCs, since they just cause warnings.  But that's
# fairly harmless.
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((cold)) cleanup(void) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_COLD], [1],
                                    [Define if __attribute__((cold))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((const)) cleanup(void) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_CONST], [1],
                                    [Define if __attribute__((const))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((noreturn)) cleanup(void) { exit(1); }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_NORETURN], [1],
                                    [Define if __attribute__((noreturn))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((format(__printf__, 1, 2))) cleanup(const char *fmt, ...) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_PRINTF], [1],
                                    [Define if __attribute__((format(__printf__)))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((unused)) cleanup(void) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_UNUSED], [1],
                                    [Define if __attribute__((unused))]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static void __attribute__((used)) cleanup(void) { }])],
			  AC_DEFINE([HAVE_ATTRIBUTE_USED], [1],
                                    [Define if __attribute__((used))]))
AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) { return __builtin_constant_p(1) ? 0 : 1; }])],
			  AC_DEFINE([HAVE_BUILTIN_CONSTANT_P], [1],
                                    [Define if have __builtin_constant_p]))
AC_LINK_IFELSE([AC_LANG_SOURCE([int main(void) { return __builtin_types_compatible_p(char *, int) ? 1 : 0; }])],
			  AC_DEFINE([HAVE_BUILTIN_TYPES_COMPATIBLE_P], [1],
                                    [Define if have __builtin_types_compatible_p]))
AC_COMPILE_IFELSE([AC_LANG_SOURCE([static int __attribute__((warn_unused_result)) func(int x) { return x; }])],
			  AC_DEFINE([HAVE_WARN_UNUSED_RESULT], [1],
                                    [Define if __attribute__((warn_unused_result))]))

if test "x$prefix" = xNONE; then
	prefix=/usr/local
fi

AC_DEFINE_UNQUOTED([CGMINER_PREFIX], ["$prefix/bin"], [Path to cgminer install])

AC_SUBST(JANSSON_LIBS)
AC_SUBST(PTHREAD_FLAGS)
AC_SUBST(DLOPEN_FLAGS)
AC_SUBST(PTHREAD_LIBS)
AC_SUBST(NCURSES_LIBS)
AC_SUBST(PDCURSES_LIBS)
AC_SUBST(WS2_LIBS)
AC_SUBST(MM_LIBS)
AC_SUBST(MATH_LIBS)
AC_SUBST(RT_LIBS)

AC_CONFIG_FILES([
	Makefile
	compat/Makefile
	ccan/Makefile
	lib/Makefile
	])
AC_OUTPUT


echo
echo
echo
echo "------------------------------------------------------------------------"
echo "$PACKAGE $VERSION"
echo "------------------------------------------------------------------------"
echo
echo
echo "Configuration Options Summary:"
echo

if test "x$libcurl" != xno; then
	echo "  libcurl(GBT).: Enabled: $LIBCURL_LIBS"
else
	echo "  libcurl(GBT).: Disabled"
fi

if test "x$libsystemd" != xno; then
	echo "  libsystemd...........: Enabled: $LIBSYSTEMD_LIBS"
else
	echo "  libsystemd...........: Disabled"
fi

echo "  curses.TUI...........: $cursesmsg"


echo
if test "x$ants1" = xyes; then
	echo "  Antminer.S1.Bitmain..: Enabled"
else
	echo "  Antminer.S1.Bitmain..: Disabled"
fi

if test "x$ants2" = xyes; then
	echo "  Antminer.S2.Bitmain..: Enabled"
else
	echo "  Antminer.S2.Bitmain..: Disabled"
fi

if test "x$ants3" = xyes; then
	echo "  Antminer.S3.Bitmain..: Enabled"
else
	echo "  Antminer.S3.Bitmain..: Disabled"
fi

if test "x$avalon" = xyes; then
	echo "  Avalon.ASICs.........: Enabled"
else
	echo "  Avalon.ASICs.........: Disabled"
fi

if test "x$avalon2" = xyes; then
	echo "  Avalon2.ASICs........: Enabled"
else
	echo "  Avalon2.ASICs........: Disabled"
fi

if test "x$avalon4" = xyes; then
	echo "  Avalon4.ASICs........: Enabled"
else
	echo "  Avalon4.ASICs........: Disabled"
fi

if test "x$avalon7" = xyes; then
	echo "  Avalon7.ASICs........: Enabled"
else
	echo "  Avalon7.ASICs........: Disabled"
fi

if test "x$avalon_miner" = xyes; then
	echo "  Avalon miner.ASICs...: Enabled"
else
	echo "  Avalon miner.ASICs...: Disabled"
fi

if test "x$minion" = xyes; then
	echo "  BlackArrowMinion.ASIC: Enabled"
else
	echo "  BlackArrowMinion.ASIC: Disabled"
fi

if test "x$bab" = xyes; then
	echo "  BlackArrow.ASICs.....: Enabled"
else
	echo "  BlackArrow.ASICs.....: Disabled"
fi

if test "x$bflsc" = xyes; then
	echo "  BFL.ASICs............: Enabled"
else
	echo "  BFL.ASICs............: Disabled"
fi

if test "x$bitforce" = xyes; then
	echo "  BitForce.FPGAs.......: Enabled"
else
	echo "  BitForce.FPGAs.......: Disabled"
fi

if test "x$bitfury" = xyes; then
	echo "  BitFury.ASICs........: Enabled"
else
	echo "  BitFury.ASICs........: Disabled"
fi

if test "x$blockerupter" = xyes; then
	echo "  BlockErupter.ASICs...: Enabled"
else
	echo "  BlockErupter.ASICs...: Disabled"
fi

if test "x$cointerra" = xyes; then
	echo "  Cointerra.ASICs......: Enabled"
else
	echo "  Cointerra.ASICs......: Disabled"
fi

if test "x$sp10" = xyes; then
	echo "  Spond-sp10.ASICs.....: Enabled"
else
	echo "  Spond-sp10.ASICs.....: Disabled"
fi


if test "x$sp30" = xyes; then
	echo "  Spond-sp30.ASICs.....: Enabled"
else
	echo "  Spond-sp30.ASICs.....: Disabled"
fi


if test "x$bitmine_A1" = xyes; then
	echo "  Bitmine-A1.ASICs.....: Enabled"
else
	echo "  Bitmine-A1.ASICs.....: Disabled"
fi

if test "x$drillbit" = xyes; then
	echo "  Drillbit.BitFury.....: Enabled"
else
	echo "  Drillbit.BitFury.....: Disabled"
fi

if test "x$hashfast" = xyes; then
	echo "  Hashfast.ASICs.......: Enabled"
else
	echo "  Hashfast.ASICs.......: Disabled"
fi

if test "x$hashratio" = xyes; then
	echo "  Hashratio.ASICs......: Enabled"
else
	echo "  Hashratio.ASICs......: Disabled"
fi

if test "x$icarus" = xyes; then
	echo "  Icarus.ASICs/FPGAs...: Enabled"
else
	echo "  Icarus.ASICs/FPGAs...: Disabled"
fi

if test "x$klondike" = xyes; then
	echo "  Klondike.ASICs.......: Enabled"
else
	echo "  Klondike.ASICs.......: Disabled"
fi

if test "x$knc" = xyes; then
	echo "  KnC.ASICs............: Enabled"
else
	echo "  KnC.ASICs............: Disabled"
fi

if test "x$modminer" = xyes; then
	echo "  ModMiner.FPGAs.......: Enabled"
else
	echo "  ModMiner.FPGAs.......: Disabled"
fi

#Add any new device to this, along with a no on the end of the test
if test "x$avalon$avalon2$avalon4$avalon7$avalon_miner$bab$bflsc$bitforce$bitfury$blockerupter$hashfast$hashratio$icarus$klondike$knc$modminer$drillbit$minion$cointerra$bitmine_A1$ants1$ants2$ants3$sp10$sp30" = xnonononononononononononononononononononononononono; then
	echo
	AC_MSG_ERROR([No mining devices configured in])
	echo
fi

if test "x$standalone" = xyes; then
	if test $drivercount != x; then
		echo
		AC_MSG_ERROR([You have configured more than one driver in with a driver that is designed to be standalone only (see ./configure --help)])
		echo
	fi
fi

echo
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  LDFLAGS..............: $LDFLAGS $PTHREAD_FLAGS"
echo "  LDADD................: $DLOPEN_FLAGS $LIBCURL_LIBS $LIBSYSTEMD_LIBS $JANSSON_LIBS $PTHREAD_LIBS $NCURSES_LIBS $PDCURSES_LIBS $WS2_LIBS $MATH_LIBS $LIBUSB_LIBS $RT_LIBS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
echo

