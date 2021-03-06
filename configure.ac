dnl  Process this file with Autoconf to produce a `configure' script.

# Package name and version.
AC_INIT([Quarry], [0.3.0], [quarry-dev@gna.org])
AC_PREREQ(2.57)

# Check if the script is running in the proper directory.  Alarm if it
# is not.
AC_CONFIG_SRCDIR(src/quarry.c)
AC_CONFIG_AUX_DIR(build)

# Initialize Automake and add maintainer mode as an option.
AM_INIT_AUTOMAKE([foreign check-news])
AM_MAINTAINER_MODE

AC_CONFIG_HEADER(config.h)

AH_TEMPLATE(PACKAGE, [Quarry])
AH_TEMPLATE(VERSION, [0.3.0])

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB


AC_SUBST(QUARRY_WARNINGS)
AC_SUBST(QUARRY_WARNINGS_GTK)

if test "$GCC" = "yes"; then
  # I would like to use `-W' too, but, unfortunately, it generates a
  # few warnings which I cannot stand.  I find it cleaner to write
  # `FIRST_XXX <= xxx && xxx <= LAST_XXX' even if `xxx' is unsigned
  # and `FIRST_XXX' is zero.  It also complains about missing
  # initializers of private fields of GTK+ structures, which I don't
  # want to touch.
  #
  # `-Wbad-function-cast' is generally good, but yells at things like
  # `(int) floor(...)', which is intolerable.

  QUARRY_WARNINGS='-Wall -Waggregate-return -Wcast-align -Wcast-qual -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wredundant-decls -Wshadow -Wstrict-prototypes -Wunused-parameter -Wwrite-strings'

  # `-Wstrict-prototypes' and `-Wshadow' launch alarms on GTK+
  # headers, so they are excluded.  `-Wwrite-strings' is excluded,
  # because certain GTK+ structures lack `const' attribute for `gchar
  # *' fields.
  #
  # `-Wcast-qual' is removed, because certain GTK+/GLib structures
  # have `gchar *' fields, which we sometimes need to assign results
  # of gettext() and friends.  Also, GLib data structures operate with
  # `gpointer's and `-Wcast-qual' generates warnings on adding
  # constant strings to them.

  QUARRY_WARNINGS_GTK='-Wall -Waggregate-return -Wcast-align -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wredundant-decls -Wunused-parameter'

else
  QUARRY_WARNINGS=''
  QUARRY_WARNINGS_GTK=''
fi


# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(limits.h float.h)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T

# Check size of some types.
AC_CHECK_SIZEOF([int], , [#include <stdio.h>])
AC_CHECK_SIZEOF([double], , [#include <stdio.h>])
AC_CHECK_SIZEOF([void *], , [#include <stdio.h>])

# Checks for library functions.
AC_FUNC_VPRINTF
AC_FUNC_MEMCMP
AC_CHECK_FUNCS(memrchr)


# Require math library.
AC_SEARCH_LIBS(floor, m)


# Find iconv().  It is usually sitting in GNU C library, but may
# sometimes be in a separate `libiconv' library.
#
# FIXME: Is this needed anymore given that we use gettext?  Check what
#	 AM_ICONV() is good for.
AC_SEARCH_LIBS(iconv, iconv)

# Turn on GNU gettext support.
AM_GNU_GETTEXT([external])


# Check for ScrollKeeper.
AC_CHECK_PROG(have_scrollkeeper, scrollkeeper-config, yes, no)
AM_CONDITIONAL(HAVE_SCROLLKEEPER, test "$have_scrollkeeper" = "yes")

if test "$have_scrollkeeper" = "yes"; then
  AC_DEFINE(HAVE_SCROLLKEEPER, 1,
	    [Define if you have ScrollKeeper package installed.])
fi


# Require GTK+ 2.0.0 or later and librsvg 2.5.0 or later.
PKG_CHECK_MODULES(QUARRY_GTK, gtk+-2.0 >= 2.0.0 librsvg-2.0 >= 2.5.0)
AC_SUBST(QUARRY_GTK_CFLAGS)
AC_SUBST(QUARRY_GTK_LIBS)

# Threads are not required for Quarry.  While Glib is probably always
# compiled with thread support, let's not insist anyway.
PKG_CHECK_MODULES(QUARRY_GTHREAD, gthread-2.0 >= 2.0.0, :, :)
AC_SUBST(QUARRY_GTHREAD_CFLAGS)
AC_SUBST(QUARRY_GTHREAD_LIBS)

# Needed to generate Quarry-specific marshallers (see
# `src/gui-gtk/Makefile.am').
AC_SUBST(GLIB_GENMARSHAL, [`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`])


# Optionally enable automatic building and installation of standalone
# SGF utilities (only `sgf-diff' at the moment).  Even if not enabled,
# the utilities can still be built manually.
AC_ARG_ENABLE(sgf-utils,
	      AC_HELP_STRING([--enable-sgf-utils],
			     [enable automatic building and installation of standalone SGF utilities]))

AM_CONDITIONAL(BUILD_SGF_UTILS, test "$enable_sgf_utils" = "yes")


# Optionally disable deprecated GTK+ code (for maintainers---to check
# if some code should be replaced with newer).
AC_ARG_ENABLE(gtk-deprecated,
	      AC_HELP_STRING([--disable-gtk-deprecated],
			     [disable deprecated GTK+ features (for maintainers; don't use for normal builds)]))

AC_SUBST(QUARRY_GTK_DEPRECATED_FLAGS)

if test "$enable_gtk_deprecated" = "no"; then

  QUARRY_GTK_DEPRECATED_FLAGS='-D G_DISABLE_DEPRECATED -D GDK_DISABLE_DEPRECATED -D GDK_PIXBUF_DISABLE_DEPRECATED -D GTK_DISABLE_DEPRECATED'

else
  QUARRY_GTK_DEPRECATED_FLAGS=''
fi


AC_ARG_ENABLE(scrollkeeper-update,
	      AC_HELP_STRING([--disable-scrollkeeper-update],
			     [don't update ScrollKeeper database (for packagers)]))

AM_CONDITIONAL(DO_SCROLLKEEPER_UPDATE,
	       [test "$have_scrollkeeper" == "yes" && test "$enable_scrollkeeper_update" != "no"])


AC_ARG_ENABLE(mime-database-update,
	      AC_HELP_STRING([--disable-mime-database-update],
			     [don't update the Shared MIME-info Database]))

AM_CONDITIONAL(REGISTER_MIME_TYPES, test "$enable_mime_database_update" != "no")


# Write `Makefile's.
AC_CONFIG_FILES(Makefile
		data/Makefile
		data/quarry.desktop
		data/markup-themes/Makefile
		data/textures/Makefile
		help/Makefile
		help/C/Makefile
		help/de/Makefile
		po/Makefile.in
		src/Makefile
		src/board/Makefile
		src/gtp/Makefile
		src/gui-gtk/Makefile
		src/gui-utils/Makefile
		src/sgf/Makefile
		src/utils/Makefile)

# The end.
AC_OUTPUT
