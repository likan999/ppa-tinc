#serial 1

dnl From Jim Meyering.
dnl Determine whether realloc works when both arguments are 0.
dnl If it doesn't, arrange to use the replacement function.
dnl
dnl If you use this macro in a package, you should
dnl add the following two lines to acconfig.h:
dnl  /* Define to rpl_realloc if the replacement function should be used.  */
dnl  #undef realloc
dnl

AC_DEFUN([jm_FUNC_REALLOC],
[
 if test x = y; then
   dnl This code is deliberately never run via ./configure.
   dnl FIXME: this is a gross hack to make autoheader put an entry
   dnl for this symbol in config.h.in.
   AC_CHECK_FUNCS(DONE_WORKING_REALLOC_CHECK)
 fi
 dnl xmalloc.c requires that this symbol be defined so it doesn't
 dnl mistakenly use a broken realloc -- as it might if this test were omitted.
 AC_DEFINE(HAVE_DONE_WORKING_REALLOC_CHECK, 1, [Needed for xmalloc.c])

 AC_CACHE_CHECK([for working realloc], jm_cv_func_working_realloc,
  [AC_RUN_IFELSE([AC_LANG_SOURCE([
    char *realloc ();
    int
    main ()
    {
      exit (realloc (0, 0) ? 0 : 1);
    }
   ])],
   [jm_cv_func_working_realloc=yes],
   [jm_cv_func_working_realloc=no],
   [When crosscompiling])
  ])
  if test $jm_cv_func_working_realloc = no; then
    AC_LIBOBJ([realloc])
    AC_DEFINE(realloc, rpl_realloc, [Replacement realloc()])
  fi
])
