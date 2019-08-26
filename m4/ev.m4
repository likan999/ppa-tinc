dnl Check to find the libev headers/libraries

AC_DEFUN([tinc_LIBEV],
[
    AC_ARG_WITH(libev,
      AS_HELP_STRING([--with-libev=DIR], [libev base directory, or:]),
      [libev="$withval"
       CPPFLAGS="$CPPFLAGS -I$withval/include"
       LDFLAGS="$LDFLAGS -L$withval/lib"]
    )

    AC_ARG_WITH(libev-include,
      AS_HELP_STRING([--with-libev-include=DIR], [libev headers directory]),
      [libev_include="$withval"
       CPPFLAGS="$CPPFLAGS -I$withval"]
    )

    AC_ARG_WITH(libev-lib,
      AS_HELP_STRING([--with-libev-lib=DIR], [libev library directory]),
      [libev_lib="$withval"
       LDFLAGS="$LDFLAGS -L$withval"]
    )

    AC_CHECK_HEADERS(ev.h,
      [],
      [AC_MSG_ERROR("ev header files not found."); break]
    )

    AC_CHECK_LIB(ev, ev_loop,
      [LIBS="$LIBS -lev"],
      [AC_MSG_ERROR("libev libraries not found.")]
    )
])
