/*
 * tclOO.c --
 *
 *	This file contains the object-system core (NB: not Tcl_Obj, but ::oo)
 *
 * Copyright (c) 2005-2006 by Donal K. Fellows
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tclOO.c,v 1.46 2008/05/20 15:44:18 dkf Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "tclInt.h"
#include "tclOOInt.h"

/*
 * Commands in oo::define.
 */

static const struct {
    const char *name;
    Tcl_ObjCmdProc *objProc;
    int flag;
} defineCmds[] = {
    {"constructor", TclOODefineConstructorObjCmd, 0},
    {"deletemethod", TclOODefineDeleteMethodObjCmd, 0},
    {"destructor", TclOODefineDestructorObjCmd, 0},
    {"export", TclOODefineExportObjCmd, 0},
    {"filter", TclOODefineFilterObjCmd, 0},
    {"forward", TclOODefineForwardObjCmd, 0},
    {"method", TclOODefineMethodObjCmd, 0},
    {"mixin", TclOODefineMixinObjCmd, 0},
    {"renamemethod", TclOODefineRenameMethodObjCmd, 0},
    {"self", TclOODefineSelfObjCmd, 0},
    {"superclass", TclOODefineSuperclassObjCmd, 0},
    {"unexport", TclOODefineUnexportObjCmd, 0},
    {NULL, NULL, 0}
}, objdefCmds[] = {
    {"class", TclOODefineClassObjCmd, 1},
    {"deletemethod", TclOODefineDeleteMethodObjCmd, 1},
    {"export", TclOODefineExportObjCmd, 1},
    {"filter", TclOODefineFilterObjCmd, 1},
    {"forward", TclOODefineForwardObjCmd, 1},
    {"method", TclOODefineMethodObjCmd, 1},
    {"mixin", TclOODefineMixinObjCmd, 1},
    {"renamemethod", TclOODefineRenameMethodObjCmd, 1},
    {"unexport", TclOODefineUnexportObjCmd, 1},
    {NULL, NULL, 0}
};

/*
 * What sort of size of things we like to allocate.
 */

#define ALLOC_CHUNK 8

/*
 * Function declarations for things defined in this file.
 */

static Class *		AllocClass(Tcl_Interp *interp, Object *useThisObj,
			    Foundation *fPtr);
static Object *		AllocObject(Foundation *fPtr, Tcl_Interp *interp,
			    const char *nameStr, const char *nsNameStr);
static int		CloneClassMethod(Tcl_Interp *interp, Class *clsPtr,
			    Method *mPtr, Tcl_Obj *namePtr,
			    Method **newMPtrPtr);
static int		CloneObjectMethod(Tcl_Interp *interp, Object *oPtr,
			    Method *mPtr, Tcl_Obj *namePtr);
static void		InitFoundation(Tcl_Interp *interp);
static void		KillFoundation(ClientData clientData,
			    Tcl_Interp *interp);
static void		ObjectNamespaceDeleted(ClientData clientData);
static void		ObjectRenamedTrace(ClientData clientData,
			    Tcl_Interp *interp, const char *oldName,
			    const char *newName, int flags);
static void		ReleaseClassContents(Tcl_Interp *interp,Object *oPtr);

static int		CopyObjectCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
static int		PublicObjectCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
static int		PrivateObjectCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);

static int		ClassCreate(ClientData clientData, Tcl_Interp *interp,
			    Tcl_ObjectContext context, int objc,
			    Tcl_Obj *const *objv);
static int		ClassCreateNs(ClientData clientData,
			    Tcl_Interp *interp, Tcl_ObjectContext context,
			    int objc, Tcl_Obj *const *objv);
static int		ClassNew(ClientData clientData, Tcl_Interp *interp,
			    Tcl_ObjectContext context, int objc,
			    Tcl_Obj *const *objv);
static int		ObjectDestroy(ClientData clientData,
			    Tcl_Interp *interp, Tcl_ObjectContext context,
			    int objc, Tcl_Obj *const *objv);
static int		ObjectEval(ClientData clientData, Tcl_Interp *interp,
			    Tcl_ObjectContext context, int objc,
			    Tcl_Obj *const *objv);
static int		ObjectLinkVar(ClientData clientData,
			    Tcl_Interp *interp, Tcl_ObjectContext context,
			    int objc, Tcl_Obj *const *objv);
static int		ObjectUnknown(ClientData clientData,
			    Tcl_Interp *interp, Tcl_ObjectContext context,
			    int objc, Tcl_Obj *const *objv);
static int		ObjectVarName(ClientData clientData,
			    Tcl_Interp *interp, Tcl_ObjectContext context,
			    int objc, Tcl_Obj *const *objv);

static int		NextObjCmd(ClientData clientData, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const *objv);
static int		SelfObjCmd(ClientData clientData, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const *objv);

/*
 * Methods in the oo::object and oo::class classes. First, we define a helper
 * macro that makes building the method type declaration structure a lot
 * easier. No point in making life harder than it has to be!
 *
 * Note that the core methods don't need clone or free proc callbacks.
 */

#define DCM(name,visibility,proc) \
    {name,visibility,\
	{TCL_OO_METHOD_VERSION_CURRENT,"core method: "#name,proc,NULL,NULL}}

static const DeclaredClassMethod objMethods[] = {
    DCM("destroy", 1, ObjectDestroy),
    DCM("eval", 0, ObjectEval),
    DCM("unknown", 0, ObjectUnknown),
    DCM("variable", 0, ObjectLinkVar),
    DCM("varname", 0, ObjectVarName),
    {NULL}
}, clsMethods[] = {
    DCM("create", 1, ClassCreate),
    DCM("new", 1, ClassNew),
    DCM("createWithNamespace", 0, ClassCreateNs),
    {NULL}
};

static char initScript[] =
    "namespace eval ::oo { variable version " TCLOO_VERSION " };"
    "namespace eval ::oo { variable patchlevel " TCLOO_PATCHLEVEL " };";
/*     "tcl_findLibrary tcloo $oo::version $oo::version" */
/*     " tcloo.tcl OO_LIBRARY oo::library;"; */

extern struct TclOOStubAPI tclOOStubAPI;

/*
 * Key into the interpreter assocData table for the foundation structure ref.
 */

#define FOUNDATION_KEY "tcl/tip257/foundation"

/*
 * ----------------------------------------------------------------------
 *
 * Tcloo_Init, Tcloo_SafeInit --
 *
 *	Called to initialise the OO system within an interpreter.
 *
 * Result:
 *	TCL_OK if the setup succeeded. Currently assumed to always work.
 *
 * Side effects:
 *	Creates namespaces, commands, several classes and a number of
 *	callbacks. Upon return, the OO system is ready for use.
 *
 * ----------------------------------------------------------------------
 */

int DLLEXPORT
Tcloo_Init(
    Tcl_Interp *interp)		/* The interpreter to install into. */
{
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
	return TCL_ERROR;
    }

    /*
     * Build the core of the OO system.
     */

    InitFoundation(interp);

    /*
     * Create non-object commands and plug ourselves into the Tcl [info]
     * ensemble.
     */

    Tcl_CreateObjCommand(interp, "::oo::Helpers::next", NextObjCmd, NULL,
	    NULL);
    Tcl_CreateObjCommand(interp, "::oo::Helpers::self", SelfObjCmd, NULL,
	    NULL);
    Tcl_CreateObjCommand(interp, "::oo::define", TclOODefineObjCmd, NULL,
	    NULL);
    Tcl_CreateObjCommand(interp, "::oo::objdefine", TclOOObjDefObjCmd, NULL,
	    NULL);
    Tcl_CreateObjCommand(interp, "::oo::copy", CopyObjectCmd, NULL, NULL);
    TclOOInitInfo(interp);

    /*
     * Run our initialization script and, if that works, declare the package
     * to be fully provided.
     */

    if (Tcl_Eval(interp, initScript) != TCL_OK) {
	return TCL_ERROR;
    }

    return Tcl_PkgProvideEx(interp, "TclOO", TCLOO_VERSION, &tclOOStubAPI);
}

int DLLEXPORT
Tcloo_SafeInit(
    Tcl_Interp *interp)		/* The safe interpreter to install into. */
{
    return Tcloo_Init(interp);
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOOGetFoundation --
 *
 *	Get a reference to the OO core class system.
 *
 * ----------------------------------------------------------------------
 */

Foundation *
TclOOGetFoundation(
    Tcl_Interp *interp)
{
    return Tcl_GetAssocData(interp, FOUNDATION_KEY, NULL);
}

/*
 * ----------------------------------------------------------------------
 *
 * InitFoundation --
 *
 *	Set up the core of the OO core class system. This is a structure
 *	holding references to the magical bits that need to be known about in
 *	other places, plus the oo::object and oo::class classes.
 *
 * ----------------------------------------------------------------------
 */

static void
InitFoundation(
    Tcl_Interp *interp)
{
    Foundation *fPtr = (Foundation *) ckalloc(sizeof(Foundation));
    Tcl_Obj *namePtr, *argsPtr, *bodyPtr;
    Tcl_DString buffer;
    int i;

    /*
     * Initialize the structure that holds the OO system core. This is
     * attached to the interpreter via an assocData entry; not very efficient,
     * but the best we can do without hacking the core more.
     */

    memset(fPtr, 0, sizeof(Foundation));
    Tcl_SetAssocData(interp, FOUNDATION_KEY, KillFoundation, fPtr);
    fPtr->interp = interp;
    fPtr->ooNs = Tcl_CreateNamespace(interp, "::oo", fPtr, NULL);
    Tcl_Export(interp, fPtr->ooNs, "[a-z]*", 1);
    fPtr->defineNs = Tcl_CreateNamespace(interp, "::oo::define", NULL, NULL);
    fPtr->objdefNs = Tcl_CreateNamespace(interp, "::oo::objdefine", NULL,
	    NULL);
    fPtr->helpersNs = Tcl_CreateNamespace(interp, "::oo::Helpers", NULL,
	    NULL);
    fPtr->epoch = 0;
    fPtr->nsCount = 0;
    fPtr->unknownMethodNameObj = Tcl_NewStringObj("unknown", -1);
    fPtr->constructorName = Tcl_NewStringObj("<constructor>", -1);
    fPtr->destructorName = Tcl_NewStringObj("<destructor>", -1);
    Tcl_IncrRefCount(fPtr->unknownMethodNameObj);
    Tcl_IncrRefCount(fPtr->constructorName);
    Tcl_IncrRefCount(fPtr->destructorName);

    /*
     * Create the subcommands in the oo::define and oo::objdefine spaces.
     */

    Tcl_DStringInit(&buffer);
    for (i=0 ; defineCmds[i].name ; i++) {
	Tcl_DStringAppend(&buffer, "::oo::define::", 14);
	Tcl_DStringAppend(&buffer, defineCmds[i].name, -1);
	Tcl_CreateObjCommand(interp, Tcl_DStringValue(&buffer),
		defineCmds[i].objProc, (void *) defineCmds[i].flag, NULL);
	Tcl_DStringFree(&buffer);
    }
    for (i=0 ; objdefCmds[i].name ; i++) {
	Tcl_DStringAppend(&buffer, "::oo::objdefine::", 17);
	Tcl_DStringAppend(&buffer, objdefCmds[i].name, -1);
	Tcl_CreateObjCommand(interp, Tcl_DStringValue(&buffer),
		objdefCmds[i].objProc, (void *) objdefCmds[i].flag, NULL);
	Tcl_DStringFree(&buffer);
    }

    /*
     * While the foundation is in assocData, this call is not needed.
     *Tcl_CallWhenDeleted(interp, KillFoundation, fPtr);
     */

    /*
     * Create the objects at the core of the object system. These need to be
     * spliced manually.
     */

    fPtr->objectCls = AllocClass(interp, AllocObject(fPtr, interp,
	    "::oo::object", NULL), fPtr);
    fPtr->classCls = AllocClass(interp, AllocObject(fPtr, interp,
	    "::oo::class", NULL), fPtr);
    fPtr->objectCls->thisPtr->selfCls = fPtr->classCls;
    fPtr->objectCls->thisPtr->flags |= ROOT_OBJECT;
    fPtr->objectCls->superclasses.num = 0;
    ckfree((char *) fPtr->objectCls->superclasses.list);
    fPtr->objectCls->superclasses.list = NULL;
    fPtr->classCls->thisPtr->selfCls = fPtr->classCls;
    TclOOAddToInstances(fPtr->objectCls->thisPtr, fPtr->classCls);
    TclOOAddToInstances(fPtr->classCls->thisPtr, fPtr->classCls);

    /*
     * Basic method declarations for the core classes.
     */

    for (i=0 ; objMethods[i].name ; i++) {
	TclOONewBasicMethod(interp, fPtr->objectCls, &objMethods[i]);
    }
    for (i=0 ; clsMethods[i].name ; i++) {
	TclOONewBasicMethod(interp, fPtr->classCls, &clsMethods[i]);
    }

    /*
     * Finish setting up the class of classes by marking the 'new' method as
     * private; classes, unlike general objects, must have explicit names. We
     * also need to create the constructor for classes.
     */

    namePtr = Tcl_NewStringObj("new", -1);
    Tcl_NewInstanceMethod(interp, (Tcl_Object) fPtr->classCls->thisPtr,
	    namePtr /* keeps ref */, 0 /* ==private */, NULL, NULL);

    argsPtr = Tcl_NewStringObj("{definitionScript {}}", -1);
    bodyPtr = Tcl_NewStringObj(
	    "if {[catch {define [self] $definitionScript} msg opt]} {\n"
	    "set ei [split [dict get $opt -errorinfo] \\n]\n"
	    "dict set opt -errorinfo [join [lrange $ei 0 end-2] \\n]\n"
	    "dict set opt -errorline 0xdeadbeef\n"
	    "}\n"
	    "return -options $opt $msg", -1);
    fPtr->classCls->constructorPtr = TclOONewProcMethod(interp,
	    fPtr->classCls, 0, NULL, argsPtr, bodyPtr, NULL);
}

/*
 * ----------------------------------------------------------------------
 *
 * KillFoundation --
 *
 *	Delete those parts of the OO core that are not deleted automatically
 *	when the objects and classes themselves are destroyed.
 *
 * ----------------------------------------------------------------------
 */

static void
KillFoundation(
    ClientData clientData,	/* Pointer to the OO system foundation
				 * structure. */
    Tcl_Interp *interp)		/* The interpreter containing the OO system
				 * foundation. */
{
    Foundation *fPtr = clientData;

    if (!Tcl_InterpDeleted(interp)) {
	Tcl_DeleteAssocData(interp, FOUNDATION_KEY);
    }

    Tcl_DecrRefCount(fPtr->unknownMethodNameObj);
    Tcl_DecrRefCount(fPtr->constructorName);
    Tcl_DecrRefCount(fPtr->destructorName);
    ckfree((char *) fPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * AllocObject --
 *
 *	Allocate an object of basic type. Does not splice the object into its
 *	class's instance list.
 *
 * ----------------------------------------------------------------------
 */

static Object *
AllocObject(
    Foundation *fPtr,		/* The basis for the object system. */
    Tcl_Interp *interp,		/* Interpreter within which to create the
				 * object. */
    const char *nameStr,	/* The name of the object to create, or NULL
				 * if the OO system should pick the object
				 * name itself (equal to the namespace
				 * name). */
    const char *nsNameStr)	/* The name of the namespace to create, or
				 * NULL if the OO system should pick a unique
				 * name itself. If this is non-NULL but names
				 * a namespace that already exists, the effect
				 * will be the same as if this was NULL. */
{
    Tcl_Obj *cmdnameObj;
    Tcl_DString buffer;
    Object *oPtr;
    int creationEpoch;

    oPtr = (Object *) ckalloc(sizeof(Object));
    memset(oPtr, 0, sizeof(Object));

    /*
     * Every object has a namespace; make one. Note that this also normally
     * computes the creation epoch value for the object, a sequence number
     * that is unique to the object (and which allows us to manage method
     * caching without comparing pointers).
     *
     * When creating a namespace, we first check to see if the caller
     * specified the name for the namespace. If not, we generate namespace
     * names using the epoch until such time as a new namespace is actually
     * created.
     */

    if (nsNameStr != NULL) {
	oPtr->namespacePtr = Tcl_CreateNamespace(interp, nsNameStr, oPtr,
		ObjectNamespaceDeleted);
	if (oPtr->namespacePtr != NULL) {
	    creationEpoch = ++fPtr->nsCount;
	    goto configNamespace;
	}
	Tcl_ResetResult(interp);
    }

    while (1) {
	char objName[10 + TCL_INTEGER_SPACE];

	sprintf(objName, "::oo::Obj%d", ++fPtr->nsCount);
	oPtr->namespacePtr = Tcl_CreateNamespace(interp, objName, oPtr,
		ObjectNamespaceDeleted);
	if (oPtr->namespacePtr != NULL) {
	    creationEpoch = fPtr->nsCount;
	    break;
	}

	/*
	 * Could not make that namespace, so we make another. But first we
	 * have to get rid of the error message from Tcl_CreateNamespace,
	 * since that's something that should not be exposed to the user.
	 */

	Tcl_ResetResult(interp);
    }

    /*
     * Make the namespace know about the helper commands. This grants access
     * to the [self] and [next] commands.
     */

  configNamespace:
    TclSetNsPath((Namespace *) oPtr->namespacePtr, 1, &fPtr->helpersNs);

    /*
     * Fill in the rest of the non-zero/NULL parts of the structure.
     */

    oPtr->fPtr = fPtr;
    oPtr->selfCls = fPtr->objectCls;
    oPtr->creationEpoch = creationEpoch;

    /*
     * Finally, create the object commands and initialize the trace on the
     * public command (so that the object structures are deleted when the
     * command is deleted).
     */

    Tcl_DStringInit(&buffer);
    if (nameStr) {
	if (nameStr[0] != ':' || nameStr[1] != ':') {
	    Tcl_DStringAppend(&buffer,
		    Tcl_GetCurrentNamespace(interp)->fullName, -1);
	    Tcl_DStringAppend(&buffer, "::", 2);
	}
	Tcl_DStringAppend(&buffer, nameStr, -1);
    } else {
	Tcl_DStringAppend(&buffer, oPtr->namespacePtr->fullName, -1);
    }
    oPtr->command = Tcl_CreateObjCommand(interp, Tcl_DStringValue(&buffer),
	    PublicObjectCmd, oPtr, NULL);
    if (nameStr) {
	Tcl_DStringFree(&buffer);
	Tcl_DStringAppend(&buffer, oPtr->namespacePtr->fullName, -1);
    }
    Tcl_DStringAppend(&buffer, "::my", 4);
    oPtr->myCommand = Tcl_CreateObjCommand(interp, Tcl_DStringValue(&buffer),
	    PrivateObjectCmd, oPtr, NULL);
    Tcl_DStringFree(&buffer);

    cmdnameObj = TclOOObjectName(interp, oPtr);
    Tcl_TraceCommand(interp, TclGetString(cmdnameObj),
	    TCL_TRACE_RENAME|TCL_TRACE_DELETE, ObjectRenamedTrace, oPtr);

    return oPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * ObjectRenamedTrace --
 *
 *	This callback is triggered when the object is deleted by any
 *	mechanism. It runs the destructors and arranges for the actual cleanup
 *	of the object's namespace, which in turn triggers cleansing of the
 *	object data structures.
 *
 * ----------------------------------------------------------------------
 */

static void
ObjectRenamedTrace(
    ClientData clientData,	/* The object being deleted. */
    Tcl_Interp *interp,		/* The interpreter containing the object. */
    const char *oldName,	/* What the object was (last) called. */
    const char *newName,	/* Always NULL. */
    int flags)			/* Why was the object deleted? */
{
    Object *oPtr = clientData;
    Class *clsPtr;

    /*
     * If this is a rename and not a delete of the object, we just flush the
     * cache of the object name.
     */

    if (flags & TCL_TRACE_RENAME) {
	if (oPtr->cachedNameObj) {
	    Tcl_DecrRefCount(oPtr->cachedNameObj);
	    oPtr->cachedNameObj = NULL;
	}
	return;
    }

    /*
     * Oh dear, the object really is being deleted. Handle this by running the
     * destructors and deleting the object's namespace, which in turn causes
     * the real object structures to be deleted.
     */

    Tcl_Preserve(oPtr);
    oPtr->flags |= OBJECT_DELETED;
    if (!Tcl_InterpDeleted(interp)) {
	CallContext *contextPtr = TclOOGetCallContext(oPtr, NULL, DESTRUCTOR);

	if (contextPtr != NULL) {
	    int result;
	    Tcl_InterpState state;

	    contextPtr->callPtr->flags |= DESTRUCTOR;
	    contextPtr->skip = 0;
	    state = Tcl_SaveInterpState(interp, TCL_OK);
	    result = TclOOInvokeContext(interp, contextPtr, 0, NULL);
	    if (result != TCL_OK) {
		Tcl_BackgroundError(interp);
	    }
	    Tcl_RestoreInterpState(interp, state);
	    TclOODeleteContext(contextPtr);
	}
    }

    /*
     * OK, the destructor's been run. Time to splat the class data (if any)
     * and nuke the namespace (which triggers the final crushing of the object
     * structure itself).
     */

    clsPtr = oPtr->classPtr;
    if (clsPtr != NULL) {
	Tcl_Preserve(clsPtr);
	ReleaseClassContents(interp, oPtr);
    }
    Tcl_DeleteNamespace(oPtr->namespacePtr);
    if (clsPtr) {
	Tcl_Release(clsPtr);
    }
    Tcl_Release(oPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * ReleaseClassContents --
 *
 *	Tear down the special class data structure, including deleting all
 *	dependent classes and objects.
 *
 * ----------------------------------------------------------------------
 */

static void
ReleaseClassContents(
    Tcl_Interp *interp,		/* The interpreter containing the class. */
    Object *oPtr)		/* The object representing the class. */
{
    int i, n;
    Class *clsPtr = oPtr->classPtr, **list;
    Object **insts;

    /*
     * Must empty list before processing the members of the list so that
     * things happen in the correct order even if something tries to play
     * fast-and-loose.
     */

    list = clsPtr->mixinSubs.list;
    n = clsPtr->mixinSubs.num;
    clsPtr->mixinSubs.list = NULL;
    clsPtr->mixinSubs.num = 0;
    clsPtr->mixinSubs.size = 0;
    for (i=0 ; i<n ; i++) {
	Tcl_Preserve(list[i]);
    }
    for (i=0 ; i<n ; i++) {
	if (!(list[i]->flags & OBJECT_DELETED) && interp != NULL) {
	    Tcl_DeleteCommandFromToken(interp, list[i]->thisPtr->command);
	}
	Tcl_Release(list[i]);
    }
    if (list != NULL) {
	ckfree((char *) list);
    }

    list = clsPtr->subclasses.list;
    n = clsPtr->subclasses.num;
    clsPtr->subclasses.list = NULL;
    clsPtr->subclasses.num = 0;
    clsPtr->subclasses.size = 0;
    for (i=0 ; i<n ; i++) {
	Tcl_Preserve(list[i]);
    }
    for (i=0 ; i<n ; i++) {
	if (!(list[i]->flags & OBJECT_DELETED) && interp != NULL) {
	    Tcl_DeleteCommandFromToken(interp, list[i]->thisPtr->command);
	}
	Tcl_Release(list[i]);
    }
    if (list != NULL) {
	ckfree((char *) list);
    }

    insts = clsPtr->instances.list;
    n = clsPtr->instances.num;
    clsPtr->instances.list = NULL;
    clsPtr->instances.num = 0;
    clsPtr->instances.size = 0;
    for (i=0 ; i<n ; i++) {
	Tcl_Preserve(insts[i]);
    }
    for (i=0 ; i<n ; i++) {
	if (!(insts[i]->flags & OBJECT_DELETED) && interp != NULL) {
	    Tcl_DeleteCommandFromToken(interp, insts[i]->command);
	}
	Tcl_Release(insts[i]);
    }
    if (insts != NULL) {
	ckfree((char *) insts);
    }

    if (clsPtr->constructorChainPtr) {
	TclOODeleteChain(clsPtr->constructorChainPtr);
    }
    if (clsPtr->destructorChainPtr) {
	TclOODeleteChain(clsPtr->destructorChainPtr);
    }

    if (clsPtr->filters.num) {
	Tcl_Obj *filterObj;

	FOREACH(filterObj, clsPtr->filters) {
	    Tcl_DecrRefCount(filterObj);
	}
	ckfree((char *) clsPtr->filters.list);
	clsPtr->filters.num = 0;
    }


    if (clsPtr->metadataPtr != NULL) {
	FOREACH_HASH_DECLS;
	Tcl_ObjectMetadataType *metadataTypePtr;
	ClientData value;

	FOREACH_HASH(metadataTypePtr, value, clsPtr->metadataPtr) {
	    metadataTypePtr->deleteProc(value);
	}
	Tcl_DeleteHashTable(clsPtr->metadataPtr);
	ckfree((char *) clsPtr->metadataPtr);
	clsPtr->metadataPtr = NULL;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ObjectNamespaceDeleted --
 *
 *	Callback when the object's namespace is deleted. Used to clean up the
 *	data structures associated with the object. The complicated bit is
 *	that this can sometimes happen before the object's command is deleted
 *	(interpreter teardown is complex!)
 *
 * ----------------------------------------------------------------------
 */

static void
ObjectNamespaceDeleted(
    ClientData clientData)	/* Pointer to the class whose namespace is
				 * being deleted. */
{
    Object *oPtr = clientData;
    FOREACH_HASH_DECLS;
    Class *clsPtr = oPtr->classPtr, *mixinPtr;
    Method *mPtr;
    Tcl_Obj *filterObj;
    int i, preserved = !(oPtr->flags & OBJECT_DELETED);

    /*
     * Instruct everyone to no longer use any allocated fields of the object.
     */

    if (preserved) {
	Tcl_Preserve(oPtr);
	if (clsPtr != NULL) {
	    Tcl_Preserve(clsPtr);
	    ReleaseClassContents(NULL, oPtr);
	}
    }
    oPtr->flags |= OBJECT_DELETED;

    /*
     * Splice the object out of its context. After this, we must *not* call
     * methods on the object.
     */

    if (!(oPtr->flags & ROOT_OBJECT)) {
	TclOORemoveFromInstances(oPtr, oPtr->selfCls);
    }

    FOREACH(mixinPtr, oPtr->mixins) {
	TclOORemoveFromInstances(oPtr, mixinPtr);
    }
    if (i) {
	ckfree((char *) oPtr->mixins.list);
    }

    FOREACH(filterObj, oPtr->filters) {
	Tcl_DecrRefCount(filterObj);
    }
    if (i) {
	ckfree((char *) oPtr->filters.list);
    }

    if (oPtr->methodsPtr) {
	FOREACH_HASH_VALUE(mPtr, oPtr->methodsPtr) {
	    TclOODeleteMethod(mPtr);
	}
	Tcl_DeleteHashTable(oPtr->methodsPtr);
	ckfree((char *) oPtr->methodsPtr);
    }

    if (oPtr->chainCache) {
	TclOODeleteChainCache(oPtr->chainCache);
    }

    if (oPtr->cachedNameObj) {
	Tcl_DecrRefCount(oPtr->cachedNameObj);
	oPtr->cachedNameObj = NULL;
    }

    if (oPtr->metadataPtr != NULL) {
	Tcl_ObjectMetadataType *metadataTypePtr;
	ClientData value;

	FOREACH_HASH(metadataTypePtr, value, oPtr->metadataPtr) {
	    metadataTypePtr->deleteProc(value);
	}
	Tcl_DeleteHashTable(oPtr->metadataPtr);
	ckfree((char *) oPtr->metadataPtr);
	oPtr->metadataPtr = NULL;
    }

    if (clsPtr != NULL && !(oPtr->flags & ROOT_OBJECT)) {
	Class *superPtr, *mixinPtr;

	if (clsPtr->metadataPtr != NULL) {
	    FOREACH_HASH_DECLS;
	    Tcl_ObjectMetadataType *metadataTypePtr;
	    ClientData value;

	    FOREACH_HASH(metadataTypePtr, value, clsPtr->metadataPtr) {
		metadataTypePtr->deleteProc(value);
	    }
	    Tcl_DeleteHashTable(clsPtr->metadataPtr);
	    ckfree((char *) clsPtr->metadataPtr);
	    clsPtr->metadataPtr = NULL;
	}

	clsPtr->flags |= OBJECT_DELETED;
	FOREACH(filterObj, clsPtr->filters) {
	    Tcl_DecrRefCount(filterObj);
	}
	if (i) {
	    ckfree((char *) clsPtr->filters.list);
	    clsPtr->filters.num = 0;
	}
	FOREACH(mixinPtr, clsPtr->mixins) {
	    if (!(mixinPtr->flags & OBJECT_DELETED)) {
		TclOORemoveFromMixinSubs(clsPtr, mixinPtr);
	    }
	}
	if (i) {
	    ckfree((char *) clsPtr->mixins.list);
	    clsPtr->mixins.num = 0;
	}
	FOREACH(superPtr, clsPtr->superclasses) {
	    if (!(superPtr->flags & OBJECT_DELETED)) {
		TclOORemoveFromSubclasses(clsPtr, superPtr);
	    }
	}
	if (i) {
	    ckfree((char *) clsPtr->superclasses.list);
	    clsPtr->superclasses.num = 0;
	}
	if (clsPtr->subclasses.list) {
	    ckfree((char *) clsPtr->subclasses.list);
	    clsPtr->subclasses.num = 0;
	}
	if (clsPtr->instances.list) {
	    ckfree((char *) clsPtr->instances.list);
	    clsPtr->instances.num = 0;
	}
	if (clsPtr->mixinSubs.list) {
	    ckfree((char *) clsPtr->mixinSubs.list);
	    clsPtr->mixinSubs.num = 0;
	}

	FOREACH_HASH_VALUE(mPtr, &clsPtr->classMethods) {
	    TclOODeleteMethod(mPtr);
	}
	Tcl_DeleteHashTable(&clsPtr->classMethods);
	TclOODeleteMethod(clsPtr->constructorPtr);
	TclOODeleteMethod(clsPtr->destructorPtr);
	Tcl_EventuallyFree(clsPtr, TCL_DYNAMIC);
    }

    /*
     * Delete the object structure itself.
     */

    Tcl_EventuallyFree(oPtr, TCL_DYNAMIC);
    if (preserved) {
	if (clsPtr) {
	    Tcl_Release(clsPtr);
	}
	Tcl_Release(oPtr);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOORemoveFromInstances --
 *
 *	Utility function to remove an object from the list of instances within
 *	a class.
 *
 * ----------------------------------------------------------------------
 */

void
TclOORemoveFromInstances(
    Object *oPtr,		/* The instance to remove. */
    Class *clsPtr)		/* The class (possibly) containing the
				 * reference to the instance. */
{
    int i;
    Object *instPtr;

    FOREACH(instPtr, clsPtr->instances) {
	if (oPtr == instPtr) {
	    goto removeInstance;
	}
    }
    return;

  removeInstance:
    clsPtr->instances.num--;
    if (i < clsPtr->instances.num) {
	clsPtr->instances.list[i] =
		clsPtr->instances.list[clsPtr->instances.num];
    }
    clsPtr->instances.list[clsPtr->instances.num] = NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOOAddToInstances --
 *
 *	Utility function to add an object to the list of instances within a
 *	class.
 *
 * ----------------------------------------------------------------------
 */

void
TclOOAddToInstances(
    Object *oPtr,		/* The instance to add. */
    Class *clsPtr)		/* The class to add the instance to. It is
				 * assumed that the class is not already
				 * present as an instance in the class. */
{
    if (clsPtr->instances.num >= clsPtr->instances.size) {
	clsPtr->instances.size += ALLOC_CHUNK;
	if (clsPtr->instances.size == ALLOC_CHUNK) {
	    clsPtr->instances.list = (Object **)
		    ckalloc(sizeof(Object *) * ALLOC_CHUNK);
	} else {
	    clsPtr->instances.list = (Object **)
		    ckrealloc((char *) clsPtr->instances.list,
		    sizeof(Object *) * clsPtr->instances.size);
	}
    }
    clsPtr->instances.list[clsPtr->instances.num++] = oPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOORemoveFromSubclasses --
 *
 *	Utility function to remove a class from the list of subclasses within
 *	another class.
 *
 * ----------------------------------------------------------------------
 */

void
TclOORemoveFromSubclasses(
    Class *subPtr,		/* The subclass to remove. */
    Class *superPtr)		/* The superclass to (possibly) remove the
				 * subclass reference from. */
{
    int i;
    Class *subclsPtr;

    FOREACH(subclsPtr, superPtr->subclasses) {
	if (subPtr == subclsPtr) {
	    goto removeSubclass;
	}
    }
    return;

  removeSubclass:
    superPtr->subclasses.num--;
    if (i < superPtr->subclasses.num) {
	superPtr->subclasses.list[i] =
		superPtr->subclasses.list[superPtr->subclasses.num];
    }
    superPtr->subclasses.list[superPtr->subclasses.num] = NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOOAddToSubclasses --
 *
 *	Utility function to add a class to the list of subclasses within
 *	another class.
 *
 * ----------------------------------------------------------------------
 */

void
TclOOAddToSubclasses(
    Class *subPtr,		/* The subclass to add. */
    Class *superPtr)		/* The superclass to add the subclass to. It
				 * is assumed that the class is not already
				 * present as a subclass in the superclass. */
{
    if (superPtr->subclasses.num >= superPtr->subclasses.size) {
	superPtr->subclasses.size += ALLOC_CHUNK;
	if (superPtr->subclasses.size == ALLOC_CHUNK) {
	    superPtr->subclasses.list = (Class **)
		    ckalloc(sizeof(Class *) * ALLOC_CHUNK);
	} else {
	    superPtr->subclasses.list = (Class **)
		    ckrealloc((char *) superPtr->subclasses.list,
		    sizeof(Class *) * superPtr->subclasses.size);
	}
    }
    superPtr->subclasses.list[superPtr->subclasses.num++] = subPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOORemoveFromMixinSubs --
 *
 *	Utility function to remove a class from the list of mixinSubs within
 *	another class.
 *
 * ----------------------------------------------------------------------
 */

void
TclOORemoveFromMixinSubs(
    Class *subPtr,		/* The subclass to remove. */
    Class *superPtr)		/* The superclass to (possibly) remove the
				 * subclass reference from. */
{
    int i;
    Class *subclsPtr;

    FOREACH(subclsPtr, superPtr->mixinSubs) {
	if (subPtr == subclsPtr) {
	    goto removeSubclass;
	}
    }
    return;

  removeSubclass:
    superPtr->mixinSubs.num--;
    if (i < superPtr->mixinSubs.num) {
	superPtr->mixinSubs.list[i] =
		superPtr->mixinSubs.list[superPtr->mixinSubs.num];
    }
    superPtr->mixinSubs.list[superPtr->mixinSubs.num] = NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOOAddToMixinSubs --
 *
 *	Utility function to add a class to the list of mixinSubs within
 *	another class.
 *
 * ----------------------------------------------------------------------
 */

void
TclOOAddToMixinSubs(
    Class *subPtr,		/* The subclass to add. */
    Class *superPtr)		/* The superclass to add the subclass to. It
				 * is assumed that the class is not already
				 * present as a subclass in the superclass. */
{
    if (superPtr->mixinSubs.num >= superPtr->mixinSubs.size) {
	superPtr->mixinSubs.size += ALLOC_CHUNK;
	if (superPtr->mixinSubs.size == ALLOC_CHUNK) {
	    superPtr->mixinSubs.list = (Class **)
		    ckalloc(sizeof(Class *) * ALLOC_CHUNK);
	} else {
	    superPtr->mixinSubs.list = (Class **)
		    ckrealloc((char *) superPtr->mixinSubs.list,
		    sizeof(Class *) * superPtr->mixinSubs.size);
	}
    }
    superPtr->mixinSubs.list[superPtr->mixinSubs.num++] = subPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * AllocClass --
 *
 *	Allocate a basic class. Does not splice the class object into its
 *	class's instance list.
 *
 * ----------------------------------------------------------------------
 */

static Class *
AllocClass(
    Tcl_Interp *interp,		/* Interpreter within which to allocate the
				 * class. */
    Object *useThisObj,		/* Object that is to act as the class
				 * representation, or NULL if a new object
				 * (with automatic name) is to be used. */
    Foundation *fPtr)		/* The foundation of the object system, passed
				 * by reference since all callers need access
				 * to it as well and that saves a lookup. */
{
    Class *clsPtr = (Class *) ckalloc(sizeof(Class));
    Tcl_Namespace *path[2];

    /*
     * Make an object if we haven't been given one.
     */

    memset(clsPtr, 0, sizeof(Class));
    if (useThisObj == NULL) {
	clsPtr->thisPtr = AllocObject(fPtr, interp, NULL, NULL);
    } else {
	clsPtr->thisPtr = useThisObj;
    }

    /*
     * Configure the namespace path for the class's object.
     */

    path[0] = fPtr->helpersNs;
    path[1] = fPtr->ooNs;
    TclSetNsPath((Namespace *) clsPtr->thisPtr->namespacePtr, 2, path);

    /*
     * Class objects inherit from the class of classes unless they inherit
     * from some subclass of it. Enforce this right now.
     */

    clsPtr->thisPtr->selfCls = fPtr->classCls;

    /*
     * Classes are subclasses of oo::object, i.e. the objects they create are
     * objects.
     */

    clsPtr->superclasses.num = 1;
    clsPtr->superclasses.list = (Class **) ckalloc(sizeof(Class *));
    clsPtr->superclasses.list[0] = fPtr->objectCls;

    /*
     * Finish connecting the class structure to the object structure.
     */

    clsPtr->thisPtr->classPtr = clsPtr;

    /*
     * That's the complicated bit. Now fill in the rest of the non-zero/NULL
     * fields.
     */

    Tcl_InitObjHashTable(&clsPtr->classMethods);
    return clsPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * Tcl_NewObjectInstance --
 *
 *	Allocate a new instance of an object.
 *
 * ----------------------------------------------------------------------
 */

Tcl_Object
Tcl_NewObjectInstance(
    Tcl_Interp *interp,		/* Interpreter context. */
    Tcl_Class cls,		/* Class to create an instance of. */
    const char *nameStr,	/* Name of object to create, or NULL to ask
				 * the code to pick its own unique name. */
    const char *nsNameStr,	/* Name of namespace to create inside object,
				 * or NULL to ask the code to pick its own
				 * unique name. */
    int objc,			/* Number of arguments. Negative value means
				 * do not call constructor. */
    Tcl_Obj *const *objv,	/* Argument list. */
    int skip)			/* Number of arguments to _not_ pass to the
				 * constructor. */
{
    Foundation *fPtr = ((Class *) cls)->thisPtr->fPtr;
    Object *oPtr = AllocObject(fPtr, interp, NULL, nsNameStr);
    CallContext *contextPtr;

    oPtr->selfCls = (Class *) cls;
    TclOOAddToInstances(oPtr, (Class *) cls);

    if (nameStr != NULL) {
	Tcl_Obj *cmdnameObj = Tcl_NewObj();
	Namespace *nsPtr, *dummy;
	const char *simpleName;
	Tcl_DString buf;
	int cmp;

	Tcl_GetCommandFullName(interp, oPtr->command, cmdnameObj);

	/*
	 * Take extra care to verify that we are not renaming a command to
	 * itself.
	 */

	TclGetNamespaceForQualName(interp, nameStr, NULL, TCL_NAMESPACE_ONLY,
		&nsPtr, &dummy, &dummy, &simpleName);
	if (simpleName != NULL && nsPtr != NULL) {
	    /*
	     * Rename the object to the name that the user wanted.
	     */

	    Tcl_DStringInit(&buf);
	    Tcl_DStringAppend(&buf, nsPtr->fullName, -1);
	    if (nsPtr->parentPtr) {
		Tcl_DStringAppend(&buf, "::", 2);
	    }
	    Tcl_DStringAppend(&buf, simpleName, -1);
	    cmp = strcmp(TclGetString(cmdnameObj), Tcl_DStringValue(&buf));
	    Tcl_DStringFree(&buf);
	    if (cmp && TclRenameCommand(interp, TclGetString(cmdnameObj),
		    nameStr) != TCL_OK) {
		Tcl_ResetResult(interp);
		Tcl_AppendResult(interp, "can't create object \"", nameStr,
			"\": command already exists with that name", NULL);
		Tcl_DecrRefCount(cmdnameObj);
		Tcl_DeleteCommandFromToken(interp, oPtr->command);
		return NULL;
	    }
	}
	Tcl_DecrRefCount(cmdnameObj);
    }

    /*
     * Check to see if we're really creating a class. If so, allocate the
     * class structure as well.
     */

    if (TclOOIsReachable(fPtr->classCls, (Class*)cls)) {
	/*
	 * Is a class, so attach a class structure. Note that the AllocClass
	 * function splices the structure into the object, so we don't have
	 * to. Once that's done, we need to repatch the object to have the
	 * right class since AllocClass interferes with that.
	 */

	AllocClass(interp, oPtr, fPtr);
	oPtr->selfCls = (Class *) cls;
	TclOOAddToSubclasses(oPtr->classPtr, fPtr->objectCls);
    }

    if (objc >= 0) {
	contextPtr = TclOOGetCallContext(oPtr, NULL, CONSTRUCTOR);
	if (contextPtr != NULL) {
	    int result;
	    Tcl_InterpState state;

	    Tcl_Preserve(oPtr);
	    state = Tcl_SaveInterpState(interp, TCL_OK);
	    contextPtr->callPtr->flags |= CONSTRUCTOR;
	    contextPtr->skip = skip;
	    result = TclOOInvokeContext(interp, contextPtr, objc, objv);
	    TclOODeleteContext(contextPtr);
	    Tcl_Release(oPtr);
	    if (result != TCL_OK) {
		Tcl_DiscardInterpState(state);
		Tcl_DeleteCommandFromToken(interp, oPtr->command);
		return NULL;
	    }
	    Tcl_RestoreInterpState(interp, state);
	}
    }

    return (Tcl_Object) oPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * Tcl_CopyObjectInstance --
 *
 *	Creates a copy of an object. Does not copy the backing namespace,
 *	since the correct way to do that (e.g., shallow/deep) depends on the
 *	object/class's own policies.
 *
 * ----------------------------------------------------------------------
 */

Tcl_Object
Tcl_CopyObjectInstance(
    Tcl_Interp *interp,
    Tcl_Object sourceObject,
    const char *targetName,
    const char *targetNamespaceName)
{
    Object *oPtr = (Object *) sourceObject, *o2Ptr;
    FOREACH_HASH_DECLS;
    Method *mPtr;
    Class *mixinPtr;
    Tcl_Obj *keyPtr, *filterObj;
    int i;

    /*
     * Sanity checks.
     */

    if (targetName == NULL && oPtr->classPtr != NULL) {
	Tcl_AppendResult(interp, "must supply a name when copying a class",
		NULL);
	return NULL;
    }
    if (oPtr->classPtr == oPtr->fPtr->classCls) {
	Tcl_AppendResult(interp, "may not clone the class of classes", NULL);
	return NULL;
    }

    /*
     * Build the instance. Note that this does not run any constructors.
     */

    o2Ptr = (Object *) Tcl_NewObjectInstance(interp,
	    (Tcl_Class) oPtr->selfCls, targetName, targetNamespaceName, -1,
	    NULL, -1);
    if (o2Ptr == NULL) {
	return NULL;
    }

    /*
     * Copy the object-local methods to the new object.
     */

    if (oPtr->methodsPtr) {
	FOREACH_HASH(keyPtr, mPtr, oPtr->methodsPtr) {
	    if (CloneObjectMethod(interp, o2Ptr, mPtr, keyPtr) != TCL_OK) {
		Tcl_DeleteCommandFromToken(interp, o2Ptr->command);
		return NULL;
	    }
	}
    }

    /*
     * Copy the object's mixin references to the new object.
     */

    FOREACH(mixinPtr, o2Ptr->mixins) {
	if (mixinPtr != o2Ptr->selfCls) {
	    TclOORemoveFromInstances(o2Ptr, mixinPtr);
	}
    }
    DUPLICATE(o2Ptr->mixins, oPtr->mixins, Class *);
    FOREACH(mixinPtr, o2Ptr->mixins) {
	if (mixinPtr != o2Ptr->selfCls) {
	    TclOOAddToInstances(o2Ptr, mixinPtr);
	}
    }

    /*
     * Copy the object's filter list to the new object.
     */

    DUPLICATE(o2Ptr->filters, oPtr->filters, Tcl_Obj *);
    FOREACH(filterObj, o2Ptr->filters) {
	Tcl_IncrRefCount(filterObj);
    }

    /*
     * Copy the object's flags to the new object, clearing those that must be
     * kept object-local. The duplicate is never deleted at this point, nor is
     * it the root of the object system or in the midst of processing a filter
     * call.
     */

    o2Ptr->flags = oPtr->flags & ~(
	    OBJECT_DELETED | ROOT_OBJECT | FILTER_HANDLING);

    /*
     * Copy the object's metadata.
     */

    if (oPtr->metadataPtr != NULL) {
	Tcl_ObjectMetadataType *metadataTypePtr;
	ClientData value, duplicate;

	FOREACH_HASH(metadataTypePtr, value, oPtr->metadataPtr) {
	    if (metadataTypePtr->cloneProc == NULL) {
		duplicate = value;
	    } else {
		if (metadataTypePtr->cloneProc(interp, value,
			&duplicate) != TCL_OK) {
		    Tcl_DeleteCommandFromToken(interp, o2Ptr->command);
		    return NULL;
		}
	    }
	    if (duplicate != NULL) {
		Tcl_ObjectSetMetadata((Tcl_Object) o2Ptr, metadataTypePtr,
			duplicate);
	    }
	}
    }

    /*
     * Copy the class, if present. Note that if there is a class present in
     * the source object, there must also be one in the copy.
     */

    if (oPtr->classPtr != NULL) {
	Class *clsPtr = oPtr->classPtr;
	Class *cls2Ptr = o2Ptr->classPtr;
	Class *superPtr;

	/*
	 * Copy the class flags across.
	 */

	cls2Ptr->flags = clsPtr->flags;

	/*
	 * Ensure that the new class's superclass structure is the same as the
	 * old class's.
	 */

	FOREACH(superPtr, cls2Ptr->superclasses) {
	    TclOORemoveFromSubclasses(cls2Ptr, superPtr);
	}
	if (cls2Ptr->superclasses.num) {
	    cls2Ptr->superclasses.list = (Class **)
		    ckrealloc((char *) cls2Ptr->superclasses.list,
		    sizeof(Class *) * clsPtr->superclasses.num);
	} else {
	    cls2Ptr->superclasses.list = (Class **)
		    ckalloc(sizeof(Class *) * clsPtr->superclasses.num);
	}
	memcpy(cls2Ptr->superclasses.list, clsPtr->superclasses.list,
		sizeof(Class *) * clsPtr->superclasses.num);
	cls2Ptr->superclasses.num = clsPtr->superclasses.num;
	FOREACH(superPtr, cls2Ptr->superclasses) {
	    TclOOAddToSubclasses(cls2Ptr, superPtr);
	}

	/*
	 * Duplicate the source class's filters.
	 */

	DUPLICATE(cls2Ptr->filters, clsPtr->filters, Tcl_Obj *);
	FOREACH(filterObj, cls2Ptr->filters) {
	    Tcl_IncrRefCount(filterObj);
	}

	/*
	 * Duplicate the source class's mixins (which cannot be circular
	 * references to the duplicate).
	 */

	FOREACH(mixinPtr, cls2Ptr->mixins) {
	    TclOORemoveFromMixinSubs(cls2Ptr, mixinPtr);
	}
	if (cls2Ptr->mixins.num != 0) {
	    ckfree((char *) clsPtr->mixins.list);
	}
	DUPLICATE(cls2Ptr->mixins, clsPtr->mixins, Class *);
	FOREACH(mixinPtr, cls2Ptr->mixins) {
	    TclOOAddToMixinSubs(cls2Ptr, mixinPtr);
	}

	/*
	 * Duplicate the source class's methods, constructor and destructor.
	 */

	FOREACH_HASH(keyPtr, mPtr, &clsPtr->classMethods) {
	    if (CloneClassMethod(interp, cls2Ptr, mPtr, keyPtr,
		    NULL) != TCL_OK) {
		Tcl_DeleteCommandFromToken(interp, o2Ptr->command);
		return NULL;
	    }
	}
	if (clsPtr->constructorPtr) {
	    if (CloneClassMethod(interp, cls2Ptr, clsPtr->constructorPtr,
		    NULL, &cls2Ptr->constructorPtr) != TCL_OK) {
		Tcl_DeleteCommandFromToken(interp, o2Ptr->command);
		return NULL;
	    }
	}
	if (clsPtr->destructorPtr) {
	    if (CloneClassMethod(interp, cls2Ptr, clsPtr->destructorPtr, NULL,
		    &cls2Ptr->destructorPtr) != TCL_OK) {
		Tcl_DeleteCommandFromToken(interp, o2Ptr->command);
		return NULL;
	    }
	}

	/*
	 * Duplicate the class's metadata.
	 */

	if (clsPtr->metadataPtr != NULL) {
	    Tcl_ObjectMetadataType *metadataTypePtr;
	    ClientData value, duplicate;

	    FOREACH_HASH(metadataTypePtr, value, clsPtr->metadataPtr) {
		if (metadataTypePtr->cloneProc == NULL) {
		    duplicate = value;
		} else {
		    if (metadataTypePtr->cloneProc(interp, value,
			    &duplicate) != TCL_OK) {
			Tcl_DeleteCommandFromToken(interp, o2Ptr->command);
			return NULL;
		    }
		}
		if (duplicate != NULL) {
		    Tcl_ClassSetMetadata((Tcl_Class) cls2Ptr, metadataTypePtr,
			    duplicate);
		}
	    }
	}
    }

    return (Tcl_Object) o2Ptr;
}

/*
 * ----------------------------------------------------------------------
 *
 * CloneObjectMethod, CloneClassMethod --
 *
 *	Helper functions used for cloning methods. They work identically to
 *	each other, except for the difference between them in how they
 *	register the cloned method on a successful clone.
 *
 * ----------------------------------------------------------------------
 */

static int
CloneObjectMethod(
    Tcl_Interp *interp,
    Object *oPtr,
    Method *mPtr,
    Tcl_Obj *namePtr)
{
    if (mPtr->typePtr == NULL) {
	Tcl_NewInstanceMethod(interp, (Tcl_Object) oPtr, namePtr,
		mPtr->flags & PUBLIC_METHOD, NULL, NULL);
    } else if (mPtr->typePtr->cloneProc) {
	ClientData newClientData;

	if (mPtr->typePtr->cloneProc(interp, mPtr->clientData,
		&newClientData) != TCL_OK) {
	    return TCL_ERROR;
	}
	Tcl_NewInstanceMethod(interp, (Tcl_Object) oPtr, namePtr,
		mPtr->flags & PUBLIC_METHOD, mPtr->typePtr, newClientData);
    } else {
	Tcl_NewInstanceMethod(interp, (Tcl_Object) oPtr, namePtr,
		mPtr->flags & PUBLIC_METHOD, mPtr->typePtr, mPtr->clientData);
    }
    return TCL_OK;
}

static int
CloneClassMethod(
    Tcl_Interp *interp,
    Class *clsPtr,
    Method *mPtr,
    Tcl_Obj *namePtr,
    Method **m2PtrPtr)
{
    Method *m2Ptr;

    if (mPtr->typePtr == NULL) {
	m2Ptr = (Method *) Tcl_NewMethod(interp, (Tcl_Class) clsPtr,
		namePtr, mPtr->flags & PUBLIC_METHOD, NULL, NULL);
    } else if (mPtr->typePtr->cloneProc) {
	ClientData newClientData;

	if (mPtr->typePtr->cloneProc(interp, mPtr->clientData,
		&newClientData) != TCL_OK) {
	    return TCL_ERROR;
	}
	m2Ptr = (Method *) Tcl_NewMethod(interp, (Tcl_Class) clsPtr,
		namePtr, mPtr->flags & PUBLIC_METHOD, mPtr->typePtr,
		newClientData);
    } else {
	m2Ptr = (Method *) Tcl_NewMethod(interp, (Tcl_Class) clsPtr,
		namePtr, mPtr->flags & PUBLIC_METHOD, mPtr->typePtr,
		mPtr->clientData);
    }
    if (m2PtrPtr != NULL) {
	*m2PtrPtr = m2Ptr;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Tcl_ClassGetMetadata, Tcl_ClassSetMetadata, Tcl_ObjectGetMetadata,
 * Tcl_ObjectSetMetadata --
 *
 *	Metadata management API. The metadata system allows code in extensions
 *	to attach arbitrary non-NULL pointers to objects and classes without
 *	the different things that might be interested being able to interfere
 *	with each other. Apart from non-NULL-ness, these routines attach no
 *	interpretation to the meaning of the metadata pointers.
 *
 *	The Tcl_*GetMetadata routines get the metadata pointer attached that
 *	has been related with a particular type, or NULL if no metadata
 *	associated with the given type has been attached.
 *
 *	The Tcl_*SetMetadata routines set or delete the metadata pointer that
 *	is related to a particular type. The value associated with the type is
 *	deleted (if present; no-op otherwise) if the value is NULL, and
 *	attached (replacing the previous value, which is deleted if present)
 *	otherwise. This means it is impossible to attach a NULL value for any
 *	metadata type.
 *
 * ----------------------------------------------------------------------
 */

ClientData
Tcl_ClassGetMetadata(
    Tcl_Class clazz,
    const Tcl_ObjectMetadataType *typePtr)
{
    Class *clsPtr = (Class *) clazz;
    Tcl_HashEntry *hPtr;

    /*
     * If there's no metadata store attached, the type in question has
     * definitely not been attached either!
     */

    if (clsPtr->metadataPtr == NULL) {
	return NULL;
    }

    /*
     * There is a metadata store, so look in it for the given type.
     */

    hPtr = Tcl_FindHashEntry(clsPtr->metadataPtr, (char *) typePtr);

    /*
     * Return the metadata value if we found it, otherwise NULL.
     */

    if (hPtr == NULL) {
	return NULL;
    }
    return Tcl_GetHashValue(hPtr);
}

void
Tcl_ClassSetMetadata(
    Tcl_Class clazz,
    const Tcl_ObjectMetadataType *typePtr,
    ClientData metadata)
{
    Class *clsPtr = (Class *) clazz;
    Tcl_HashEntry *hPtr;
    int isNew;

    /*
     * Attach the metadata store if not done already.
     */

    if (clsPtr->metadataPtr == NULL) {
	if (metadata == NULL) {
	    return;
	}
	clsPtr->metadataPtr = (Tcl_HashTable*) ckalloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(clsPtr->metadataPtr, TCL_ONE_WORD_KEYS);
    }

    /*
     * If the metadata is NULL, we're deleting the metadata for the type.
     */

    if (metadata == NULL) {
	hPtr = Tcl_FindHashEntry(clsPtr->metadataPtr, (char *) typePtr);
	if (hPtr != NULL) {
	    typePtr->deleteProc(Tcl_GetHashValue(hPtr));
	    Tcl_DeleteHashEntry(hPtr);
	}
	return;
    }

    /*
     * Otherwise we're attaching the metadata. Note that if there was already
     * some metadata attached of this type, we delete that first.
     */

    hPtr = Tcl_CreateHashEntry(clsPtr->metadataPtr, (char *) typePtr, &isNew);
    if (!isNew) {
	typePtr->deleteProc(Tcl_GetHashValue(hPtr));
    }
    Tcl_SetHashValue(hPtr, metadata);
}

ClientData
Tcl_ObjectGetMetadata(
    Tcl_Object object,
    const Tcl_ObjectMetadataType *typePtr)
{
    Object *oPtr = (Object *) object;
    Tcl_HashEntry *hPtr;

    /*
     * If there's no metadata store attached, the type in question has
     * definitely not been attached either!
     */

    if (oPtr->metadataPtr == NULL) {
	return NULL;
    }

    /*
     * There is a metadata store, so look in it for the given type.
     */

    hPtr = Tcl_FindHashEntry(oPtr->metadataPtr, (char *) typePtr);

    /*
     * Return the metadata value if we found it, otherwise NULL.
     */

    if (hPtr == NULL) {
	return NULL;
    }
    return Tcl_GetHashValue(hPtr);
}

void
Tcl_ObjectSetMetadata(
    Tcl_Object object,
    const Tcl_ObjectMetadataType *typePtr,
    ClientData metadata)
{
    Object *oPtr = (Object *) object;
    Tcl_HashEntry *hPtr;
    int isNew;

    /*
     * Attach the metadata store if not done already.
     */

    if (oPtr->metadataPtr == NULL) {
	if (metadata == NULL) {
	    return;
	}
	oPtr->metadataPtr = (Tcl_HashTable *) ckalloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(oPtr->metadataPtr, TCL_ONE_WORD_KEYS);
    }

    /*
     * If the metadata is NULL, we're deleting the metadata for the type.
     */

    if (metadata == NULL) {
	hPtr = Tcl_FindHashEntry(oPtr->metadataPtr, (char *) typePtr);
	if (hPtr != NULL) {
	    typePtr->deleteProc(Tcl_GetHashValue(hPtr));
	    Tcl_DeleteHashEntry(hPtr);
	}
	return;
    }

    /*
     * Otherwise we're attaching the metadata. Note that if there was already
     * some metadata attached of this type, we delete that first.
     */

    hPtr = Tcl_CreateHashEntry(oPtr->metadataPtr, (char *) typePtr, &isNew);
    if (!isNew) {
	typePtr->deleteProc(Tcl_GetHashValue(hPtr));
    }
    Tcl_SetHashValue(hPtr, metadata);
}

/*
 * ----------------------------------------------------------------------
 *
 * PublicObjectCmd, PrivateObjectCmd, TclOOInvokeObject, TclOOObjectCmdCore --
 *
 *	Main entry point for object invokations. The Public* and Private*
 *	wrapper functions are just thin wrappers round the main
 *	TclOOObjectCmdCore function that does call chain creation, management
 *	and invokation.
 *
 * ----------------------------------------------------------------------
 */

static int
PublicObjectCmd(
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const *objv)
{
    return TclOOObjectCmdCore(clientData, interp, objc, objv, PUBLIC_METHOD,
	    NULL);
}

static int
PrivateObjectCmd(
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const *objv)
{
    return TclOOObjectCmdCore(clientData, interp, objc, objv, 0, NULL);
}

int
TclOOInvokeObject(
    Tcl_Interp *interp,		/* Interpreter for commands, variables,
				 * results, error reporting, etc. */
    Tcl_Object object,		/* The object to invoke. */
    Tcl_Class startCls,		/* Where in the class chain to start the
				 * invoke from, or NULL to traverse the whole
				 * chain including filters. */
    int publicPrivate,		/* Whether this is an invoke from a public
				 * context (PUBLIC_METHOD), a private context
				 * (PRIVATE_METHOD), or a *really* private
				 * context (any other value; conventionally
				 * 0). */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* Array of argument objects. It is assumed
				 * that the name of the method to invoke will
				 * be at index 1. */
{
    switch (publicPrivate) {
    case PUBLIC_METHOD:
	return TclOOObjectCmdCore((Object *) object, interp, objc, objv,
		PUBLIC_METHOD, (Class *) startCls);
    case PRIVATE_METHOD:
	return TclOOObjectCmdCore((Object *) object, interp, objc, objv,
		PRIVATE_METHOD, (Class *) startCls);
    default:
	return TclOOObjectCmdCore((Object *) object, interp, objc, objv, 0,
		(Class *) startCls);
    }
}

int
TclOOObjectCmdCore(
    Object *oPtr,		/* The object being invoked. */
    Tcl_Interp *interp,		/* The interpreter containing the object. */
    int objc,			/* How many arguments are being passed in. */
    Tcl_Obj *const *objv,	/* The array of arguments. */
    int flags,			/* Whether this is an invokation through the
				 * public or the private command interface. */
    Class *startCls)		/* Where to start in the call chain, or NULL
				 * if we are to start at the front with
				 * filters and the object's methods (which is
				 * the normal case). */
{
    CallContext *contextPtr;
    Tcl_Obj *methodNamePtr;
    int result;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "method ?arg ...?");
	return TCL_ERROR;
    }

    /*
     * Give plugged in code a chance to remap the method name.
     */

    methodNamePtr = objv[1];
    if (oPtr->mapMethodNameProc != NULL) {
	register Class **startClsPtr = &startCls;

	methodNamePtr = Tcl_DuplicateObj(methodNamePtr);
	result = oPtr->mapMethodNameProc(interp, (Tcl_Object) oPtr,
		(Tcl_Class *) startClsPtr, methodNamePtr);
	if (result != TCL_OK) {
	    if (result == TCL_ERROR) {
		Tcl_AddErrorInfo(interp, "\n    (while mapping method name)");
	    }
	    Tcl_DecrRefCount(methodNamePtr);
	    return result;
	}
    }
    Tcl_IncrRefCount(methodNamePtr);

    /*
     * Get the call chain.
     */

    contextPtr = TclOOGetCallContext(oPtr, methodNamePtr,
	    flags | (oPtr->flags & FILTER_HANDLING));
    if (contextPtr == NULL) {
	Tcl_AppendResult(interp, "impossible to invoke method \"",
		TclGetString(methodNamePtr),
		"\": no defined method or unknown method", NULL);
	Tcl_DecrRefCount(methodNamePtr);
	return TCL_ERROR;
    }
    Tcl_Preserve(oPtr);

    /*
     * Check to see if we need to apply magical tricks to start part way
     * through the call chain.
     */

    if (startCls != NULL) {
	while (contextPtr->index < contextPtr->callPtr->numChain) {
	    register struct MInvoke *miPtr =
		    &contextPtr->callPtr->chain[contextPtr->index];

	    if (miPtr->isFilter || miPtr->mPtr->declaringClassPtr!=startCls) {
		contextPtr->index++;
	    } else {
		break;
	    }
	}
	if (contextPtr->index >= contextPtr->callPtr->numChain) {
	    result = TCL_ERROR;
	    Tcl_SetResult(interp, "no valid method implementation",
		    TCL_STATIC);
	    goto disposeChain;
	}
    }

    /*
     * Invoke the call chain, locking the object structure against deletion
     * for the duration.
     */

    result = TclOOInvokeContext(interp, contextPtr, objc, objv);

    /*
     * Dispose of the call chain, either back into the ether or into the
     * chain cache.
     */

  disposeChain:
    if (!(contextPtr->callPtr->flags & OO_UNKNOWN_METHOD)
	    && !(oPtr->flags & OBJECT_DELETED)) {
	TclOOStashContext(methodNamePtr, contextPtr);
    }
    TclOODeleteContext(contextPtr);

    /*
     * Drop the lock on the object's structure.
     */

    Tcl_Release(oPtr);
    Tcl_DecrRefCount(methodNamePtr);
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * ClassCreate --
 *
 *	Implementation for oo::class->create method.
 *
 * ----------------------------------------------------------------------
 */

static int
ClassCreate(
    ClientData clientData,	/* Ignored. */
    Tcl_Interp *interp,		/* Interpreter in which to create the object;
				 * also used for error reporting. */
    Tcl_ObjectContext context,	/* The object/call context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* The actual arguments. */
{
    Object *oPtr = (Object *) Tcl_ObjectContextObject(context);
    Tcl_Object newObject;
    const char *objName;
    int len;

    /*
     * Sanity check; should not be possible to invoke this method on a
     * non-class.
     */

    if (oPtr->classPtr == NULL) {
	Tcl_Obj *cmdnameObj = TclOOObjectName(interp, oPtr);

	Tcl_AppendResult(interp, "object \"", TclGetString(cmdnameObj),
		"\" is not a class", NULL);
	return TCL_ERROR;
    }

    /*
     * Check we have the right number of (sensible) arguments.
     */

    if (objc - Tcl_ObjectContextSkippedArgs(context) < 1) {
	Tcl_WrongNumArgs(interp, Tcl_ObjectContextSkippedArgs(context), objv,
		"objectName ?arg ...?");
	return TCL_ERROR;
    }
    objName = Tcl_GetStringFromObj(
	    objv[Tcl_ObjectContextSkippedArgs(context)], &len);
    if (len == 0) {
	Tcl_AppendResult(interp, "object name must not be empty", NULL);
	return TCL_ERROR;
    }

    /*
     * Make the object and return its name.
     */

    newObject = Tcl_NewObjectInstance(interp, (Tcl_Class) oPtr->classPtr,
	    objName, NULL, objc, objv,
	    Tcl_ObjectContextSkippedArgs(context)+1);
    if (newObject == NULL) {
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, TclOOObjectName(interp, (Object *) newObject));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ClassCreateNs --
 *
 *	Implementation for oo::class->createWithNamespace method.
 *
 * ----------------------------------------------------------------------
 */

static int
ClassCreateNs(
    ClientData clientData,	/* Ignored. */
    Tcl_Interp *interp,		/* Interpreter in which to create the object;
				 * also used for error reporting. */
    Tcl_ObjectContext context,	/* The object/call context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* The actual arguments. */
{
    Object *oPtr = (Object *) Tcl_ObjectContextObject(context);
    Tcl_Object newObject;
    const char *objName, *nsName;
    int len;

    /*
     * Sanity check; should not be possible to invoke this method on a
     * non-class.
     */

    if (oPtr->classPtr == NULL) {
	Tcl_Obj *cmdnameObj = TclOOObjectName(interp, oPtr);

	Tcl_AppendResult(interp, "object \"", TclGetString(cmdnameObj),
		"\" is not a class", NULL);
	return TCL_ERROR;
    }

    /*
     * Check we have the right number of (sensible) arguments.
     */

    if (objc - Tcl_ObjectContextSkippedArgs(context) < 2) {
	Tcl_WrongNumArgs(interp, Tcl_ObjectContextSkippedArgs(context), objv,
		"objectName namespaceName ?arg ...?");
	return TCL_ERROR;
    }
    objName = Tcl_GetStringFromObj(
	    objv[Tcl_ObjectContextSkippedArgs(context)], &len);
    if (len == 0) {
	Tcl_AppendResult(interp, "object name must not be empty", NULL);
	return TCL_ERROR;
    }
    nsName = Tcl_GetStringFromObj(
	    objv[Tcl_ObjectContextSkippedArgs(context)+1], &len);
    if (len == 0) {
	Tcl_AppendResult(interp, "namespace name must not be empty", NULL);
	return TCL_ERROR;
    }

    /*
     * Make the object and return its name.
     */

    newObject = Tcl_NewObjectInstance(interp, (Tcl_Class) oPtr->classPtr,
	    objName, nsName, objc, objv,
	    Tcl_ObjectContextSkippedArgs(context)+2);
    if (newObject == NULL) {
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, TclOOObjectName(interp, (Object *) newObject));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ClassNew --
 *
 *	Implementation for oo::class->new method.
 *
 * ----------------------------------------------------------------------
 */

static int
ClassNew(
    ClientData clientData,	/* Ignored. */
    Tcl_Interp *interp,		/* Interpreter in which to create the object;
				 * also used for error reporting. */
    Tcl_ObjectContext context,	/* The object/call context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* The actual arguments. */
{
    Object *oPtr = (Object *) Tcl_ObjectContextObject(context);
    Tcl_Object newObject;

    /*
     * Sanity check; should not be possible to invoke this method on a
     * non-class.
     */

    if (oPtr->classPtr == NULL) {
	Tcl_Obj *cmdnameObj = TclOOObjectName(interp, oPtr);

	Tcl_AppendResult(interp, "object \"", TclGetString(cmdnameObj),
		"\" is not a class", NULL);
	return TCL_ERROR;
    }

    /*
     * Make the object and return its name.
     */

    newObject = Tcl_NewObjectInstance(interp, (Tcl_Class) oPtr->classPtr,
	    NULL, NULL, objc, objv, Tcl_ObjectContextSkippedArgs(context));
    if (newObject == NULL) {
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, TclOOObjectName(interp, (Object *) newObject));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ObjectDestroy --
 *
 *	Implementation for oo::object->destroy method.
 *
 * ----------------------------------------------------------------------
 */

static int
ObjectDestroy(
    ClientData clientData,	/* Ignored. */
    Tcl_Interp *interp,		/* Interpreter in which to create the object;
				 * also used for error reporting. */
    Tcl_ObjectContext context,	/* The object/call context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* The actual arguments. */
{
    if (objc != Tcl_ObjectContextSkippedArgs(context)) {
	Tcl_WrongNumArgs(interp, Tcl_ObjectContextSkippedArgs(context), objv,
		NULL);
	return TCL_ERROR;
    }
    Tcl_DeleteCommandFromToken(interp,
	    Tcl_GetObjectCommand(Tcl_ObjectContextObject(context)));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ObjectEval --
 *
 *	Implementation for oo::object->eval method.
 *
 * ----------------------------------------------------------------------
 */

static int
ObjectEval(
    ClientData clientData,	/* Ignored. */
    Tcl_Interp *interp,		/* Interpreter in which to create the object;
				 * also used for error reporting. */
    Tcl_ObjectContext context,	/* The object/call context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* The actual arguments. */
{
    CallContext *contextPtr = (CallContext *) context;
    Tcl_Object object = Tcl_ObjectContextObject(context);
    CallFrame *framePtr, **framePtrPtr;
    Tcl_Obj *objnameObj;
    int result;

    if (objc-1 < Tcl_ObjectContextSkippedArgs(context)) {
	Tcl_WrongNumArgs(interp, Tcl_ObjectContextSkippedArgs(context), objv,
		"arg ?arg ...?");
	return TCL_ERROR;
    }

    /*
     * Make the object's namespace the current namespace and evaluate the
     * command(s).
     */

    /* This is needed to satisfy GCC 3.3's strict aliasing rules */
    framePtrPtr = &framePtr;
    result = TclPushStackFrame(interp, (Tcl_CallFrame **) framePtrPtr,
	    Tcl_GetObjectNamespace(object), 0);
    if (result != TCL_OK) {
	return TCL_ERROR;
    }
    framePtr->objc = objc;
    framePtr->objv = objv;	/* Reference counts do not need to be
				 * incremented here. */

    if (contextPtr->callPtr->flags & PUBLIC_METHOD) {
	objnameObj = TclOOObjectName(interp, (Object *) object);
    } else {
	objnameObj = Tcl_NewStringObj("my", 2);
    }
    Tcl_IncrRefCount(objnameObj);

    if (objc == Tcl_ObjectContextSkippedArgs(context)+1) {
	result = Tcl_EvalObjEx(interp,
		objv[Tcl_ObjectContextSkippedArgs(context)], 0);
    } else {
	Tcl_Obj *objPtr;

	/*
	 * More than one argument: concatenate them together with spaces
	 * between, then evaluate the result. Tcl_EvalObjEx will delete the
	 * object when it decrements its refcount after eval'ing it.
	 */

	objPtr = Tcl_ConcatObj(objc-Tcl_ObjectContextSkippedArgs(context),
		objv+Tcl_ObjectContextSkippedArgs(context));
	result = Tcl_EvalObjEx(interp, objPtr, TCL_EVAL_DIRECT);
    }

    if (result == TCL_ERROR) {
	Tcl_AppendObjToErrorInfo(interp, Tcl_ObjPrintf(
		"\n    (in \"%s eval\" script line %d)",
		TclGetString(objnameObj), interp->errorLine));
    }

    /*
     * Restore the previous "current" namespace.
     */

    TclPopStackFrame(interp);
    Tcl_DecrRefCount(objnameObj);
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * ObjectUnknown --
 *
 *	Default unknown method handler method (defined in oo::object). This
 *	just creates a suitable error message.
 *
 * ----------------------------------------------------------------------
 */

static int
ObjectUnknown(
    ClientData clientData,	/* Ignored. */
    Tcl_Interp *interp,		/* Interpreter in which to create the object;
				 * also used for error reporting. */
    Tcl_ObjectContext context,	/* The object/call context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* The actual arguments. */
{
    CallContext *contextPtr = (CallContext *) context;
    Object *oPtr = contextPtr->oPtr;
    const char **methodNames;
    int numMethodNames, i, skip = Tcl_ObjectContextSkippedArgs(context);

    if (objc < skip+1) {
	Tcl_WrongNumArgs(interp, skip, objv, "methodName ?arg ...?");
	return TCL_ERROR;
    }

    /*
     * Get the list of methods that we want to know about.
     */

    numMethodNames = TclOOGetSortedMethodList(oPtr,
	    contextPtr->callPtr->flags & PUBLIC_METHOD, &methodNames);

    /*
     * Special message when there are no visible methods at all.
     */

    if (numMethodNames == 0) {
	Tcl_Obj *tmpBuf = TclOOObjectName(interp, oPtr);

	Tcl_AppendResult(interp, "object \"", TclGetString(tmpBuf), NULL);
	if (contextPtr->callPtr->flags & PUBLIC_METHOD) {
	    Tcl_AppendResult(interp, "\" has no visible methods", NULL);
	} else {
	    Tcl_AppendResult(interp, "\" has no methods", NULL);
	}
	return TCL_ERROR;
    }

    Tcl_AppendResult(interp, "unknown method \"", TclGetString(objv[skip]),
	    "\": must be ", NULL);
    for (i=0 ; i<numMethodNames-1 ; i++) {
	if (i) {
	    Tcl_AppendResult(interp, ", ", NULL);
	}
	Tcl_AppendResult(interp, methodNames[i], NULL);
    }
    if (i) {
	Tcl_AppendResult(interp, " or ", NULL);
    }
    Tcl_AppendResult(interp, methodNames[i], NULL);
    ckfree((char *) methodNames);
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * ObjectLinkVar --
 *
 *	Implementation of oo::object->variable method.
 *
 * ----------------------------------------------------------------------
 */

static int
ObjectLinkVar(
    ClientData clientData,	/* Ignored. */
    Tcl_Interp *interp,		/* Interpreter in which to create the object;
				 * also used for error reporting. */
    Tcl_ObjectContext context,	/* The object/call context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* The actual arguments. */
{
    Interp *iPtr = (Interp *) interp;
    Tcl_Object object = Tcl_ObjectContextObject(context);
    Namespace *savedNsPtr;
    int i;

    if (objc-Tcl_ObjectContextSkippedArgs(context) < 1) {
	Tcl_WrongNumArgs(interp, Tcl_ObjectContextSkippedArgs(context), objv,
		"varName ?varName ...?");
	return TCL_ERROR;
    }

    /*
     * Do nothing if we are not called from the body of a method. In this
     * respect, we are like the [global] command.
     */

    if (iPtr->varFramePtr == NULL ||
	    !(iPtr->varFramePtr->isProcCallFrame & FRAME_IS_METHOD)) {
	return TCL_OK;
    }

    for (i=Tcl_ObjectContextSkippedArgs(context) ; i<objc ; i++) {
	Var *varPtr, *aryPtr;
	const char *varName = TclGetString(objv[i]);

	/*
	 * The variable name must not contain a '::' since that's illegal in
	 * local names.
	 */

	if (strstr(varName, "::") != NULL) {
	    Tcl_AppendResult(interp, "variable name \"", varName,
		    "\" illegal: must not contain namespace separator", NULL);
	    return TCL_ERROR;
	}

	/*
	 * Switch to the object's namespace for the duration of this call.
	 * Like this, the variable is looked up in the namespace of the
	 * object, and not in the namespace of the caller. Otherwise this
	 * would only work if the caller was a method of the object itself,
	 * which might not be true if the method was exported. This is a bit
	 * of a hack, but the simplest way to do this (pushing a stack frame
	 * would be horribly expensive by comparison). We never have to worry
	 * about the case where we're dealing with the global namespace; we've
	 * already checked that we are inside a method.
	 */

	savedNsPtr = iPtr->varFramePtr->nsPtr;
	iPtr->varFramePtr->nsPtr = (Namespace *)
		Tcl_GetObjectNamespace(object);
	varPtr = TclObjLookupVar(interp, objv[i], NULL, TCL_NAMESPACE_ONLY,
		"define", 1, 0, &aryPtr);
	iPtr->varFramePtr->nsPtr = savedNsPtr;

	if (varPtr == NULL || aryPtr != NULL) {
	    /*
	     * Variable cannot be an element in an array. If aryPtr is not
	     * NULL, it is an element, so throw up an error and return.
	     */

	    TclVarErrMsg(interp, varName, NULL, "define",
		    "name refers to an element in an array");
	    return TCL_ERROR;
	}

	/*
	 * Arrange for the lifetime of the variable to be correctly managed.
	 * This is copied out of Tcl_VariableObjCmd...
	 */

	if (!TclIsVarNamespaceVar(varPtr)) {
	    TclSetVarNamespaceVar(varPtr);
	}

	if (TclPtrMakeUpvar(interp, varPtr, varName, 0, -1) != TCL_OK) {
	    return TCL_ERROR;
	}
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ObjectVarName --
 *
 *	Implementation of the oo::object->varname method.
 *
 * ----------------------------------------------------------------------
 */

static int
ObjectVarName(
    ClientData clientData,	/* Ignored. */
    Tcl_Interp *interp,		/* Interpreter in which to create the object;
				 * also used for error reporting. */
    Tcl_ObjectContext context,	/* The object/call context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* The actual arguments. */
{
    Interp *iPtr = (Interp *) interp;
    Var *varPtr, *aryVar;
    Tcl_Obj *varNamePtr;

    if (Tcl_ObjectContextSkippedArgs(context)+1 != objc) {
	Tcl_WrongNumArgs(interp, Tcl_ObjectContextSkippedArgs(context), objv,
		"varName");
	return TCL_ERROR;
    }

    /*
     * Switch to the object's namespace for the duration of this call. Like
     * this, the variable is looked up in the namespace of the object, and not
     * in the namespace of the caller. Otherwise this would only work if the
     * caller was a method of the object itself, which might not be true if
     * the method was exported. This is a bit of a hack, but the simplest way
     * to do this (pushing a stack frame would be horribly expensive by
     * comparison, and is only done when we'd otherwise interfere with the
     * global namespace).
     */

    if (iPtr->varFramePtr == NULL) {
	Tcl_CallFrame *dummyFrame;

	TclPushStackFrame(interp, &dummyFrame,
		Tcl_GetObjectNamespace(Tcl_ObjectContextObject(context)),0);
	varPtr = TclObjLookupVar(interp, objv[objc-1], NULL,
		TCL_NAMESPACE_ONLY|TCL_LEAVE_ERR_MSG, "refer to",1,1,&aryVar);
	TclPopStackFrame(interp);
    } else {
	Namespace *savedNsPtr;

	savedNsPtr = iPtr->varFramePtr->nsPtr;
	iPtr->varFramePtr->nsPtr = (Namespace *)
		Tcl_GetObjectNamespace(Tcl_ObjectContextObject(context));
	varPtr = TclObjLookupVar(interp, objv[objc-1], NULL,
		TCL_NAMESPACE_ONLY|TCL_LEAVE_ERR_MSG, "refer to",1,1,&aryVar);
	iPtr->varFramePtr->nsPtr = savedNsPtr;
    }

    if (varPtr == NULL) {
	return TCL_ERROR;
    }

    varNamePtr = Tcl_NewObj();
    Tcl_GetVariableFullName(interp, (Tcl_Var) varPtr, varNamePtr);
    Tcl_SetObjResult(interp, varNamePtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Tcl_ObjectContextInvokeNext --
 *
 *	Invokes the next stage of the call chain described in an object
 *	context. This is the core of the implementation of the [next] command.
 *	Does not do management of the call-frame stack.
 *
 * ----------------------------------------------------------------------
 */

int
Tcl_ObjectContextInvokeNext(
    Tcl_Interp *interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj *const *objv,
    int skip)
{
    CallContext *contextPtr = (CallContext *) context;
    int savedIndex = contextPtr->index;
    int savedSkip = contextPtr->skip;
    int result;

    if (contextPtr->index+1 >= contextPtr->callPtr->numChain) {
	/*
	 * We're at the end of the chain; return the empty string (the most
	 * useful thing we can do, since it turns out that it's not always
	 * trivial to detect in source code whether there is a parent
	 * implementation, what with multiple-inheritance...)
	 */

	return TCL_OK;
    }

    /*
     * Advance to the next method implementation in the chain in the method
     * call context while we process the body. However, need to adjust the
     * argument-skip control because we're guaranteed to have a single prefix
     * arg (i.e., 'next') and not the variable amount that can happen because
     * method invokations (i.e., '$obj meth' and 'my meth'), constructors
     * (i.e., '$cls new' and '$cls create obj') and destructors (no args at
     * all) come through the same code.
     */

    contextPtr->index++;
    contextPtr->skip = skip;

    /*
     * Invoke the (advanced) method call context in the caller context.
     */

    result = TclOOInvokeContext(interp, contextPtr, objc, objv);

    /*
     * Restore the call chain context index as we've finished the inner invoke
     * and want to operate in the outer context again.
     */

    contextPtr->index = savedIndex;
    contextPtr->skip = savedSkip;

    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * NextObjCmd --
 *
 *	Implementation of the [next] command. Note that this command is only
 *	ever to be used inside the body of a procedure-like method.
 *
 * ----------------------------------------------------------------------
 */

static int
NextObjCmd(
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const *objv)
{
    Interp *iPtr = (Interp *) interp;
    CallFrame *framePtr = iPtr->varFramePtr;
    Tcl_ObjectContext context;
    int result;

    /*
     * Start with sanity checks on the calling context to make sure that we
     * are invoked from a suitable method context. If so, we can safely
     * retrieve the handle to the object call context.
     */

    if (framePtr == NULL || !(framePtr->isProcCallFrame & FRAME_IS_METHOD)) {
	Tcl_AppendResult(interp, TclGetString(objv[0]),
		" may only be called from inside a method", NULL);
	return TCL_ERROR;
    }
    context = framePtr->clientData;

    /*
     * Invoke the (advanced) method call context in the caller context. Note
     * that this is like [uplevel 1] and not [eval].
     */

    iPtr->varFramePtr = framePtr->callerVarPtr;
    result = Tcl_ObjectContextInvokeNext(interp, context, objc, objv, 1);
    iPtr->varFramePtr = framePtr;
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * SelfObjCmd --
 *
 *	Implementation of the [self] command, which provides introspection of
 *	the call context.
 *
 * ----------------------------------------------------------------------
 */

static int
SelfObjCmd(
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const *objv)
{
    static const char *subcmds[] = {
	"caller", "class", "filter", "method", "namespace", "next", "object",
	"target", NULL
    };
    enum SelfCmds {
	SELF_CALLER, SELF_CLASS, SELF_FILTER, SELF_METHOD, SELF_NS, SELF_NEXT,
	SELF_OBJECT, SELF_TARGET
    };
    Interp *iPtr = (Interp *) interp;
    CallFrame *framePtr = iPtr->varFramePtr;
    CallContext *contextPtr;
    int index;

    /*
     * Start with sanity checks on the calling context and the method context.
     */

    if (framePtr == NULL || !(framePtr->isProcCallFrame & FRAME_IS_METHOD)) {
	Tcl_AppendResult(interp, TclGetString(objv[0]),
		" may only be called from inside a method", NULL);
	return TCL_ERROR;
    }

    contextPtr = framePtr->clientData;

    /*
     * Now we do "conventional" argument parsing for a while. Note that no
     * subcommand takes arguments.
     */

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "subcommand");
	return TCL_ERROR;
    } else if (objc == 1) {
	index = SELF_OBJECT;
    } else if (Tcl_GetIndexFromObj(interp, objv[1], subcmds, "subcommand", 0,
	    &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum SelfCmds) index) {
    case SELF_OBJECT:
	Tcl_SetObjResult(interp, TclOOObjectName(interp, contextPtr->oPtr));
	return TCL_OK;
    case SELF_NS:
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		contextPtr->oPtr->namespacePtr->fullName,-1));
	return TCL_OK;
    case SELF_CLASS: {
	Method *mPtr = contextPtr->callPtr->chain[contextPtr->index].mPtr;
	Object *declarerPtr;

	if (mPtr->declaringClassPtr != NULL) {
	    declarerPtr = mPtr->declaringClassPtr->thisPtr;
	} else if (mPtr->declaringObjectPtr != NULL) {
	    declarerPtr = mPtr->declaringObjectPtr;
	} else {
	    /*
	     * This should be unreachable code.
	     */

	    Tcl_AppendResult(interp, "method without declarer!", NULL);
	    return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, TclOOObjectName(interp, declarerPtr));
	return TCL_OK;
    }
    case SELF_METHOD:
	if (contextPtr->callPtr->flags & CONSTRUCTOR) {
	    Tcl_AppendResult(interp, "<constructor>", NULL);
	} else if (contextPtr->callPtr->flags & DESTRUCTOR) {
	    Tcl_AppendResult(interp, "<destructor>", NULL);
	} else {
	    Method *mPtr = contextPtr->callPtr->chain[contextPtr->index].mPtr;

	    Tcl_SetObjResult(interp, mPtr->namePtr);
	}
	return TCL_OK;
    case SELF_FILTER:
	if (!contextPtr->callPtr->chain[contextPtr->index].isFilter) {
	    Tcl_AppendResult(interp, "not inside a filtering context", NULL);
	    return TCL_ERROR;
	} else {
	    register struct MInvoke *miPtr =
		    &contextPtr->callPtr->chain[contextPtr->index];
	    Tcl_Obj *result[3];
	    Object *oPtr;
	    const char *type;

	    if (miPtr->filterDeclarer != NULL) {
		oPtr = miPtr->filterDeclarer->thisPtr;
		type = "class";
	    } else {
		oPtr = contextPtr->oPtr;
		type = "object";
	    }

	    result[0] = TclOOObjectName(interp, oPtr);
	    result[1] = Tcl_NewStringObj(type, -1);
	    result[2] = miPtr->mPtr->namePtr;
	    Tcl_SetObjResult(interp, Tcl_NewListObj(3, result));
	    return TCL_OK;
	}
    case SELF_CALLER:
	if ((framePtr->callerVarPtr != NULL) &&
		(framePtr->callerVarPtr->isProcCallFrame & FRAME_IS_METHOD)) {
	    CallContext *callerPtr = framePtr->callerVarPtr->clientData;
	    Method *mPtr = callerPtr->callPtr->chain[callerPtr->index].mPtr;
	    Object *declarerPtr;

	    if (mPtr->declaringClassPtr != NULL) {
		declarerPtr = mPtr->declaringClassPtr->thisPtr;
	    } else if (mPtr->declaringObjectPtr != NULL) {
		declarerPtr = mPtr->declaringObjectPtr;
	    } else {
		/*
		 * This should be unreachable code.
		 */

		Tcl_AppendResult(interp, "method without declarer!", NULL);
		return TCL_ERROR;
	    }

	    Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
		    TclOOObjectName(interp, declarerPtr));
	    Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
		    TclOOObjectName(interp, callerPtr->oPtr));
	    if (callerPtr->callPtr->flags & CONSTRUCTOR) {
		Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			Tcl_NewStringObj("<constructor>", -1));
	    } else if (callerPtr->callPtr->flags & DESTRUCTOR) {
		Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			Tcl_NewStringObj("<destructor>", -1));
	    } else {
		Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			mPtr->namePtr);
	    }
	    return TCL_OK;
	} else {
	    Tcl_AppendResult(interp, "caller is not an object", NULL);
	    return TCL_ERROR;
	}
    case SELF_NEXT:
	if (contextPtr->index < contextPtr->callPtr->numChain-1) {
	    Method *mPtr =
		    contextPtr->callPtr->chain[contextPtr->index+1].mPtr;
	    Object *declarerPtr;

	    if (mPtr->declaringClassPtr != NULL) {
		declarerPtr = mPtr->declaringClassPtr->thisPtr;
	    } else if (mPtr->declaringObjectPtr != NULL) {
		declarerPtr = mPtr->declaringObjectPtr;
	    } else {
		/*
		 * This should be unreachable code.
		 */

		Tcl_AppendResult(interp, "method without declarer!", NULL);
		return TCL_ERROR;
	    }

	    Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
		    TclOOObjectName(interp, declarerPtr));
	    if (contextPtr->callPtr->flags & CONSTRUCTOR) {
		Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			Tcl_NewStringObj("<constructor>", -1));
	    } else if (contextPtr->callPtr->flags & DESTRUCTOR) {
		Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			Tcl_NewStringObj("<destructor>", -1));
	    } else {
		Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			mPtr->namePtr);
	    }
	}
	return TCL_OK;
    case SELF_TARGET:
	if (!contextPtr->callPtr->chain[contextPtr->index].isFilter) {
	    Tcl_AppendResult(interp, "not inside a filtering context", NULL);
	    return TCL_ERROR;
	} else {
	    Method *mPtr;
	    Object *declarerPtr;
	    int i;

	    for (i=contextPtr->index ; i<contextPtr->callPtr->numChain ; i++){
		if (!contextPtr->callPtr->chain[i].isFilter) {
		    break;
		}
	    }
	    if (i == contextPtr->callPtr->numChain) {
		Tcl_Panic("filtering call chain without terminal non-filter");
	    }
	    mPtr = contextPtr->callPtr->chain[i].mPtr;
	    if (mPtr->declaringClassPtr != NULL) {
		declarerPtr = mPtr->declaringClassPtr->thisPtr;
	    } else if (mPtr->declaringObjectPtr != NULL) {
		declarerPtr = mPtr->declaringObjectPtr;
	    } else {
		/*
		 * This should be unreachable code.
		 */

		Tcl_AppendResult(interp, "method without declarer!", NULL);
		return TCL_ERROR;
	    }
	    Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
		    TclOOObjectName(interp, declarerPtr));
	    Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
		    mPtr->namePtr);
	    return TCL_OK;
	}
    }
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * CopyObjectCmd --
 *
 *	Implementation of the [oo::copy] command, which clones an object (but
 *	not its namespace). Note that no constructors are called during this
 *	process.
 *
 * ----------------------------------------------------------------------
 */

static int
CopyObjectCmd(
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const *objv)
{
    Tcl_Object oPtr, o2Ptr;

    if (objc < 2 || objc > 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "sourceName ?targetName?");
	return TCL_ERROR;
    }

    oPtr = Tcl_GetObjectFromObj(interp, objv[1]);
    if (oPtr == NULL) {
	return TCL_ERROR;
    }

    /*
     * Create a cloned object of the correct class. Note that constructors are
     * not called. Also note that we must resolve the object name ourselves
     * because we do not want to create the object in the current namespace,
     * but rather in the context of the namespace of the caller of the overall
     * [oo::define] command.
     */

    if (objc == 2) {
	o2Ptr = Tcl_CopyObjectInstance(interp, oPtr, NULL, NULL);
    } else {
	char *name;
	Tcl_DString buffer;

	name = TclGetString(objv[2]);
	Tcl_DStringInit(&buffer);
	if (name[0]!=':' || name[1]!=':') {
	    Interp *iPtr = (Interp *) interp;

	    if (iPtr->varFramePtr != NULL) {
		Tcl_DStringAppend(&buffer,
			iPtr->varFramePtr->nsPtr->fullName, -1);
	    }
	    Tcl_DStringAppend(&buffer, "::", 2);
	    Tcl_DStringAppend(&buffer, name, -1);
	    name = Tcl_DStringValue(&buffer);
	}
	o2Ptr = Tcl_CopyObjectInstance(interp, oPtr, name, NULL);
	Tcl_DStringFree(&buffer);
    }

    if (o2Ptr == NULL) {
	return TCL_ERROR;
    }

    /*
     * Return the name of the cloned object.
     */

    Tcl_SetObjResult(interp, TclOOObjectName(interp, (Object *) o2Ptr));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Tcl_GetObjectFromObj --
 *
 *	Utility function to get an object from a Tcl_Obj containing its name.
 *
 * ----------------------------------------------------------------------
 */

Tcl_Object
Tcl_GetObjectFromObj(
    Tcl_Interp *interp,		/* Interpreter in which to locate the object.
				 * Will have an error message placed in it if
				 * the name does not refer to an object. */
    Tcl_Obj *objPtr)		/* The name of the object to look up, which is
				 * exactly the name of its public command. */
{
    Command *cmdPtr = (Command *) Tcl_GetCommandFromObj(interp, objPtr);

    if (cmdPtr == NULL) {
	goto notAnObject;
    }
    if (cmdPtr->objProc != PublicObjectCmd) {
	cmdPtr = (Command *) TclGetOriginalCommand((Tcl_Command) cmdPtr);
	if (cmdPtr == NULL || cmdPtr->objProc != PublicObjectCmd) {
	    goto notAnObject;
	}
    }
    return cmdPtr->objClientData;

  notAnObject:
    Tcl_AppendResult(interp, TclGetString(objPtr),
	    " does not refer to an object", NULL);
    return NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOOIsReachable --
 *
 *	Utility function that tests whether a class is a subclass (whether
 *	directly or indirectly) of another class.
 *
 * ----------------------------------------------------------------------
 */

int
TclOOIsReachable(
    Class *targetPtr,
    Class *startPtr)
{
    int i;
    Class *superPtr;

  tailRecurse:
    if (startPtr == targetPtr) {
	return 1;
    }
    if (startPtr->superclasses.num == 1 && startPtr->mixins.num == 0) {
	startPtr = startPtr->superclasses.list[0];
	goto tailRecurse;
    }
    FOREACH(superPtr, startPtr->superclasses) {
	if (TclOOIsReachable(targetPtr, superPtr)) {
	    return 1;
	}
    }
    FOREACH(superPtr, startPtr->mixins) {
	if (TclOOIsReachable(targetPtr, superPtr)) {
	    return 1;
	}
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOOObjectName --
 *
 *	Utility function that returns the name of the object. Note that this
 *	simplifies cache management by keeping the code to do it in one place
 *	and not sprayed all over. The value returned always has a reference
 *	count of at least one.
 *
 * ----------------------------------------------------------------------
 */

Tcl_Obj *
TclOOObjectName(
    Tcl_Interp *interp,
    Object *oPtr)
{
    Tcl_Obj *namePtr;

    if (oPtr->cachedNameObj) {
	return oPtr->cachedNameObj;
    }
    namePtr = Tcl_NewObj();
    Tcl_GetCommandFullName(interp, oPtr->command, namePtr);
    Tcl_IncrRefCount(namePtr);
    oPtr->cachedNameObj = namePtr;
    return namePtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * assorted trivial 'getter' functions
 *
 * ----------------------------------------------------------------------
 */

Tcl_Method
Tcl_ObjectContextMethod(
    Tcl_ObjectContext context)
{
    CallContext *contextPtr = (CallContext *) context;
    return (Tcl_Method) contextPtr->callPtr->chain[contextPtr->index].mPtr;
}

int
Tcl_ObjectContextIsFiltering(
    Tcl_ObjectContext context)
{
    CallContext *contextPtr = (CallContext *) context;
    return contextPtr->callPtr->chain[contextPtr->index].isFilter;
}

Tcl_Object
Tcl_ObjectContextObject(
    Tcl_ObjectContext context)
{
    return (Tcl_Object) ((CallContext *)context)->oPtr;
}

int
Tcl_ObjectContextSkippedArgs(
    Tcl_ObjectContext context)
{
    return ((CallContext *)context)->skip;
}

Tcl_Namespace *
Tcl_GetObjectNamespace(
    Tcl_Object object)
{
    return ((Object *)object)->namespacePtr;
}

Tcl_Command
Tcl_GetObjectCommand(
    Tcl_Object object)
{
    return ((Object *)object)->command;
}

Tcl_Class
Tcl_GetObjectAsClass(
    Tcl_Object object)
{
    return (Tcl_Class) ((Object *)object)->classPtr;
}

int
Tcl_ObjectDeleted(
    Tcl_Object object)
{
    return (((Object *)object)->flags & OBJECT_DELETED) ? 1 : 0;
}

Tcl_Object
Tcl_GetClassAsObject(
    Tcl_Class clazz)
{
    return (Tcl_Object) ((Class *)clazz)->thisPtr;
}

Tcl_ObjectMapMethodNameProc
Tcl_ObjectGetMethodNameMapper(
    Tcl_Object object)
{
    return ((Object *) object)->mapMethodNameProc;
}

void
Tcl_ObjectSetMethodNameMapper(
    Tcl_Object object,
    Tcl_ObjectMapMethodNameProc mapMethodNameProc)
{
    ((Object *) object)->mapMethodNameProc = mapMethodNameProc;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
