AC_PREREQ(2.50)
builtin(include, tclconfig/tcl.m4)
AC_DEFUN([TEAX_SUBST_RESOURCE], [
    dirs="$1"
    vars="$2"
dnl rcdef_inc=-I		# For testing only
dnl rcdef_start=-D		# For testing only
    AS_IF([test ${TEA_PLATFORM} = "windows"], [
	AC_CHECK_PROGS(RC, windres rc, none)
	case $RC in
	    windres)
		rcdef_inc="--include "
		rcdef_start="--define "
		rcdef_q='\"'
		AC_SUBST(RES_SUFFIX, [res.o])
		PKG_OBJECTS="${PKG_OBJECTS} ${PACKAGE_NAME}.res.o" ;;
	    rc)
		rcdef_inc="/i "
		rcdef_start="/d "
		rcdef_q='"'
		AC_SUBST(RES_SUFFIX, [res])
		PKG_OBJECTS="${PKG_OBJECTS} ${PACKAGE_NAME}.res" ;;
	    none)
		AC_MSG_WARN([could not find resource compiler])
		RC=: ;;
	esac])
    for i in $dirs; do
	RES_DEFS="${RES_DEFS} ${rcdef_inc}\"`${CYGPATH} $i`\""
    done
    for i in $vars; do
	RES_DEFS="$RES_DEFS ${rcdef_start}$i='${rcdef_q}\$($i)${rcdef_q}'"
    done
    AC_SUBST(RES_DEFS)])
AC_DEFUN([TEAX_ADD_PRIVATE_HEADERS], [
    vars="$@"
    for i in $vars; do
	# check for existence, be strict because it should be present!
	AS_IF([test ! -f "${srcdir}/$i"], [
	    AC_MSG_ERROR([could not find header file '${srcdir}/$i'])])
	PKG_PRIVATE_HEADERS="$PKG_PRIVATE_HEADERS $i"
    done
    AC_SUBST(PKG_PRIVATE_HEADERS)])
dnl Extra magic to make things work with Vista and VC
AC_DEFUN([TEAX_VC_MANIFEST], [
    CC_OUT="-o [\$]@"
    AC_SUBST(CC_OUT)
    AS_IF([test "$GCC" != yes \
	    -a ${TEA_PLATFORM} == "windows" \
	    -a "${SHARED_BUILD}" = "1"], [
	# This refers to "Manifest Tool" not "Magnetic Tape utility"
	AC_CHECK_PROGS(MT, mt, none)
	AS_IF([test "$MT" != none], [
	    CC_OUT="/Fo[\$]@"
	    ADD_MANIFEST="${MT} -manifest [\$]@.manifest -outputresource:[\$]@\;2"
	    AC_SUBST(ADD_MANIFEST)
	    CLEANFILES="$CLEANFILES ${PKG_LIB_FILE}.manifest"])])])
AC_DEFUN([TEAX_SDX], [
    AC_PATH_PROG(SDX, sdx, none)
    AS_IF([test "${SDX}" = "none"],[
	AC_PATH_PROG(SDX_KIT, sdx.kit, none)
	AS_IF([test "${SDX_KIT}" != "none"],[
	    # We assume that sdx.kit is on the path, and that the default
	    # tclsh is activetcl
	    SDX="tclsh '${SDX_KIT}'"])])
    AS_IF([test "${SDX}" = "none"],[
	AC_MSG_WARN([cannot find sdx; building starkits will fail])
	AC_MSG_NOTICE([building as a normal library still supported])])])
dnl TODO: Adapt this for OSX Frameworks...
dnl This next bit is a bit ugly, but it makes things for tclooConfig.sh...
AC_DEFUN([TEAX_INCLUDE_LINE], [
    eval "$1=\"-I`${CYGPATH} $2`\""
    AC_SUBST($1)])
AC_DEFUN([TEAX_LINK_LINE], [
    AS_IF([test ${TCL_LIB_VERSIONS_OK} = nodots], [
	eval "$1=\"-L`${CYGPATH} $2` -l$3${TCL_TRIM_DOTS}\""
    ], [
	eval "$1=\"-L`${CYGPATH} $2` -l$3${PACKAGE_VERSION}\""
    ])
    AC_SUBST($1)])

dnl Local Variables:
dnl mode: autoconf
dnl End:
