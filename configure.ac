m4_define(FULL_PAVER, esyscmd(/usr/bin/pkg-config --silence-errors --modversion pulsecore | tr -d \\n))
m4_define(PAVER, regexp(FULL_PAVER, [\([0123456789.]+\).*], [\1]))

AC_PREREQ(2.61)

AC_INIT([pulseaudio-murphy-ivi],[PAVER],[janos.kovacs@intel.com])

AC_CONFIG_SRCDIR([module-murphy-ivi/module-murphy-ivi.c])
AC_CONFIG_HEADER([foo.h])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign 1.8.5 -Wall])


# this would not be really needed if everything would be as it should be
AS_IF([test x${exec_prefix} = xNONE], [LIBDIR=$ac_default_prefix/lib], [LIBDIR=${libdir}])
AC_SUBST(LIBDIR)

AC_SUBST(PA_MAJORMINOR, [PAVER])

AC_PROG_MKDIR_P
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_GCC_TRADITIONAL
AC_USE_SYSTEM_EXTENSIONS
AM_PROG_AR

AC_PATH_PROG([M4], [m4 gm4], [no])
if test "x$M4" = xno ; then
   AC_MSG_ERROR([m4 missing])
fi

AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

#dnl Compiler flags
#DESIRED_FLAGS="-Wall -W -Wextra -pedantic -pipe -Wno-long-long -Wvla -Wno-overlength-strings -Wconversion -Wundef -Wformat -Wlogical-op -Wpacked -Wformat-security -Wmissing-include-dirs -Wformat-nonliteral -Wold-style-definition -Wdeclaration-after-statement -Wfloat-equal -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls -Wmissing-noreturn -Wshadow -Wendif-labels -Wpointer-arith -Wcast-align -Wwrite-strings -Wno-unused-parameter -ffast-math"

#for flag in $DESIRED_FLAGS ; do
#  CC_CHECK_CFLAGS([$flag], [CFLAGS="$CFLAGS $flag"])
#done

AC_LTDL_ENABLE_INSTALL
AC_LIBLTDL_INSTALLABLE
AC_LIBTOOL_DLOPEN
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL
AC_SUBST(LTDLINCL)
AC_SUBST(LIBLTDL)
AC_CONFIG_SUBDIRS(libltdl)

AC_HEADER_STDC
AC_C_CONST
AC_C_BIGENDIAN
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_TYPES(ssize_t, , [AC_DEFINE([ssize_t], [signed long],
    [Define ssize_t if it is not done by the standard libs.])])
AC_TYPE_OFF_T
AC_TYPE_SIGNAL
AC_TYPE_UID_T
AC_CHECK_DECLS(environ)

AC_DEFINE_UNQUOTED(PA_CFLAGS,"$CFLAGS", [The CFLAGS used during compilation])

PKG_CHECK_MODULES(LIBPULSE, [libpulse])
AC_SUBST(LIBPULSE_CFLAGS)
AC_SUBST(LIBPULSE_LIBS)

PKG_CHECK_MODULES(PULSEDEVEL, [pulseaudio-module-devel])
AC_SUBST(PULSEDEVEL_CFLAGS)
AC_SUBST(PULSEDEVEL_LIBS)
PKG_CHECK_VAR([modlibexecdir], [pulseaudio-module-devel], [modlibexecdir])
AC_SUBST([modlibexecdir])

#### murphy policy module (optional) ####
AC_ARG_ENABLE([murphy],
    AS_HELP_STRING([--enable-murphy], [Enable optional murphy policy module]), [enable_murphy=yes], [enable_murphy=no])

AS_IF([test x$enable_murphy != xno],
    [PKG_CHECK_MODULES(MURPHY, [murphy-domain-controller murphy-pulse, murphy-common murphy-lua-utils lua >= 5.1.1 json-c], HAVE_MURPHY=1, HAVE_MURPHY=0)],
    HAVE_MURPHY=0)

AS_IF([test x$enable_murphy = xyes && test x$HAVE_MURPHY = "x0"],
    [AC_MSG_ERROR([*** Murphy support not found])])

AC_SUBST(HAVE_MURPHY)
AC_SUBST(MURPHY_CFLAGS)
AC_SUBST(MURPHY_LIBS)
AM_CONDITIONAL(HAVE_MURPHY, [test "x$HAVE_MURPHY" = x1])
AS_IF([test "x$HAVE_MURPHY" = "x1"], AC_DEFINE([HAVE_MURPHY], 1, [Have Murphy policy module]))

# documentation
AC_ARG_WITH([documentation],
            [AS_HELP_STRING([--with-documentation],
                            [generate pdf, html and other doc files])],
            [],
            [with_documentation=auto]
)

AS_IF( [ test x$with_documentation = xno ],
       [ has_doc_tools="no" ],
       [ AC_PATH_TOOL([PA_DOXYGEN], doxygen)
         AC_PATH_TOOL([PA_LYX], lyx)
         AC_PATH_TOOL([PA_INKSCAPE], inkscape)
         AC_PATH_TOOL([PA_PYTHON], python)
         AC_PATH_TOOL([PA_TOUCH], touch)
         AC_PATH_TOOL([PA_DBLATEX], dblatex)
         AC_PATH_TOOL([PA_XMLTO], xmlto)

         AS_IF( [ test x$PA_DOXYGEN = x -o x$PA_LYX = x -o \
                       x$PA_INKSCAPE = x -o x$PA_PYTHON = x -o \
                       x$PA_TOUCH = x],
                [ has_doc_tools="no";
                  AC_MSG_WARN([Some essential doc-tool is missing]) ],
                [ has_doc_tools="yes";
                  PA_DOCINIT() ]
         ) ]
)

AS_IF( [ test x$has_doc_tools == "xno" -o x$PA_DBLATEX = x ],
       [ can_make_pdfs="no";
         AC_WARN([No PDF documentation will be generated]) ],
       [ can_make_pdfs="yes"]
)

AS_IF([ test x$has_doc_tools == "xno" -o x$PA_XMLTO = x ],
      [ can_make_html="no";
        AC_WARN([No HTML documentation will be generated]) ],
      [ can_make_html="yes" ]
)


AM_CONDITIONAL(BUILD_DOCUMENTATION,  [ test x$has_doc_tools = "xyes" ])
AM_CONDITIONAL(BUILD_PDF_DOCUMENTS,  [ test x$can_make_pdfs = "xyes" ])
AM_CONDITIONAL(BUILD_HTML_DOCUMENTS, [ test x$can_make_html = "xyes" ])

AC_SUBST(PA_DOCDIR, [$top_srcdir/doc])
AC_SUBST(PA_FIGDIR, [$PA_DOCDIR/common/figures])
AC_SUBST(PA_MAKE_DOCRULES, [$PA_DOCDIR/Makefile.rules])
AC_SUBST(PA_DOCSCRIPT_DIR, [$PA_DOCDIR/scripts])
AC_SUBST(PA_DBLYXFIX, [$PA_DOCSCRIPT_DIR/dblyxfix.py])
AC_SUBST(PA_DOXML2DB, [$PA_DOCSCRIPT_DIR/doxml2db.py])
AC_SUBST(PA_DOXYDEPS, [$PA_DOCSCRIPT_DIR/doxydeps.py])


# Shave by default
SHAVE_INIT([build-aux], [enable])

AC_CONFIG_FILES([
        build-aux/shave
        build-aux/shave-libtool
	Makefile
	module-murphy-ivi/Makefile
        combine/Makefile
        augment/Makefile
        doc/Makefile
        doc/murphy-audio/Makefile
        doc/murphy-audio/db/Makefile
])
AC_OUTPUT

echo "
 ---{ $PACKAGE_NAME $VERSION }---

    prefix:               ${prefix}
    sysconfdir:           ${sysconfdir}
    localstatedir:        ${localstatedir}
    modlibexecdir:        ${modlibexecdir}
    Compiler:             ${CC}
    CFLAGS:               ${CFLAGS}
    LIBDIR:               ${LIBDIR}
    LIBPULSE_CFLAGS:      ${LIBPULSE_CFLAGS}
    LIBPULSE_LIBS:        ${LIBPULSE_LIBS}
    PULSEDEVEL_CFLAGS     ${PULSEDEVEL_CFLAGS}
    PULSEDEVEL_LIBS       ${PULSEDEVEL_LIBS}
    MAJORMINOR:           ${PA_MAJORMINOR}
    MURPHY_CFLAGS         ${MURPHY_CFLAGS}
    MURPHY_LIBS           ${MURPHY_LIBS}
    murphy enabled:       $enable_murphy
"
