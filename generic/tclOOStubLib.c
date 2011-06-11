/*
 * ORIGINAL SOURCE: tk/generic/tkStubLib.c, version 1.9 2004/03/17
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * Need to ensure that this file is built without any external references.
 */

#undef USE_TCL_STUBS
#undef USE_TCLOO_STUBS
#define USE_TCL_STUBS 1
#define USE_TCLOO_STUBS 1

#include "tcl.h"
#include "tclOO.h"
#include "tclOOInt.h"

MODULE_SCOPE const TclOOStubs *tclOOStubsPtr;
MODULE_SCOPE const TclOOIntStubs *tclOOIntStubsPtr;

const TclOOStubs *tclOOStubsPtr = NULL;
const TclOOIntStubs *tclOOIntStubsPtr = NULL;

/*
 *----------------------------------------------------------------------
 *
 * support functions --
 *	These ensure that this file has no dependence on the version of the C
 *	library that was used during the build (an issue on Windows).
 *
 *----------------------------------------------------------------------
 */

static inline int
isDigit(
    const int c)
{
    return (c >= '0' && c <= '9');	/* Assume ASCII */
}

static inline const char *
RequireExactVersion(
    Tcl_Interp *interp,
    const char *packageName,
    const char *desiredVersion,
    const char *actualVersion)
{
    const char *p = desiredVersion;
    int count = 0;

    while (*p) {
	count += !isDigit(*p++);
    }
    if (count == 1) {
	const char *q = actualVersion;

	p = desiredVersion;
	while (*p && (*p == *q)) {
	    p++; q++;
	}
	if (*p) {
	    /* Construct error message */
	    Tcl_PkgRequireEx(interp, packageName, desiredVersion, 1, NULL);
	    return NULL;
	}
    } else {
	actualVersion = Tcl_PkgRequireEx(interp, packageName, desiredVersion,
		1, NULL);
	if (actualVersion == NULL) {
	    return NULL;
	}
    }
    return actualVersion;
}

/*
 *----------------------------------------------------------------------
 *
 * TclOOInitializeStubs --
 *	Load the tclOO package, initialize stub table pointer. Do not call
 *	this function directly, use Tcl_OOInitStubs() macro instead.
 *
 * Results:
 *	The actual version of the package that satisfies the request, or NULL
 *	to indicate that an error occurred.
 *
 * Side effects:
 *	Sets the stub table pointer.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE const char *
TclOOInitializeStubs(
    Tcl_Interp *interp,
    const char *version,
    int exact)
{
    const TclOOStubs **stubsPtrPtr = &tclOOStubsPtr;
    const char *gotVer = Tcl_PkgRequireEx(interp, "TclOO", version, 0,
	    (ClientData *) stubsPtrPtr);

    if (gotVer == NULL) {
	return NULL;
    }

    /* Cargo-culted logic alert! */
    if (exact) {
	gotVer = RequireExactVersion(interp, "TclOO", version, gotVer);
	if (gotVer == NULL) {
	    return NULL;
	}
    }

    if (tclOOStubsPtr == NULL) {
	Tcl_ResetResult(interp);
	Tcl_AppendResult(interp, "Error loading TclOO package; ",
		"package not present or incomplete", NULL);
	return NULL;
    }

    tclOOIntStubsPtr = tclOOStubsPtr->hooks->tclOOIntStubs;
    return gotVer;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
