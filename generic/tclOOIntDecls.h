/*
 * $Id: tclOOIntDecls.h,v 1.1 2007/05/18 13:19:19 dkf Exp $
 *
 * This file is (mostly) automatically generated from tclOO.decls.
 */

/* !BEGIN!: Do not edit below this line. */

#define TCLOOINT_STUBS_EPOCH 0
#define TCLOOINT_STUBS_REVISION 24

#if !defined(USE_TCLOO_STUBS)

/*
 * Exported function declarations:
 */

/* 0 */
TCLOOAPI Tcl_Object	TclOOGetDefineCmdContext (Tcl_Interp * interp);

#endif /* !defined(USE_TCLOO_STUBS) */

typedef struct TclOOIntStubs {
    int magic;
    int epoch;
    int revision;
    struct TclOOIntStubHooks *hooks;

    Tcl_Object (*tclOOGetDefineCmdContext) (Tcl_Interp * interp); /* 0 */
} TclOOIntStubs;

#ifdef __cplusplus
extern "C" {
#endif
extern const TclOOIntStubs *tclOOIntStubsPtr;
#ifdef __cplusplus
}
#endif

#if defined(USE_TCLOO_STUBS)

/*
 * Inline function declarations:
 */

#ifndef TclOOGetDefineCmdContext
#define TclOOGetDefineCmdContext \
	(tclOOIntStubsPtr->tclOOGetDefineCmdContext) /* 0 */
#endif

#endif /* defined(USE_TCLOO_STUBS) */

/* !END!: Do not edit above this line. */

struct TclOOStubAPI {
    TclOOStubs *stubsPtr;
    TclOOIntStubs *intStubsPtr;
};
