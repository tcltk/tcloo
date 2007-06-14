/*
 * tclOOMethod.c --
 *
 *	This file contains code to create and manage methods.
 *
 * Copyright (c) 2005-2006 by Donal K. Fellows
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tclOOMethod.c,v 1.2 2007/06/14 21:03:55 msofer Exp $
 */

#include "tclInt.h"
#include "tclOOInt.h"

/*
 * Structure used to help delay computing names of objects or classes for
 * [info frame] until needed, making invokation faster in the normal case.
 */

struct PNI {
    Tcl_Interp *interp;
    Tcl_Method method;
};

/*
 * Function declarations for things defined in this file.
 */

static Tcl_Obj **	InitEnsembleRewrite(Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv, int toRewrite,
			    int rewriteLength, Tcl_Obj *const *rewriteObjs,
			    int *lengthPtr);
static int		InvokeProcedureMethod(ClientData clientData,
			    Tcl_Interp *interp, Tcl_ObjectContext context,
			    int objc, Tcl_Obj *const *objv);
static void		DeleteProcedureMethod(ClientData clientData);
static int		CloneProcedureMethod(ClientData clientData,
			    ClientData *newClientData);
static void		MethodErrorHandler(Tcl_Interp *interp,
			    Tcl_Obj *procNameObj);
static void		ConstructorErrorHandler(Tcl_Interp *interp,
			    Tcl_Obj *procNameObj);
static void		DestructorErrorHandler(Tcl_Interp *interp,
			    Tcl_Obj *procNameObj);
static Tcl_Obj *	RenderDeclarerName(ClientData clientData);

static int		InvokeForwardMethod(ClientData clientData,
			    Tcl_Interp *interp, Tcl_ObjectContext context,
			    int objc, Tcl_Obj *const *objv);
static void		DeleteForwardMethod(ClientData clientData);
static int		CloneForwardMethod(ClientData clientData,
			    ClientData *newClientData);

static int		BasicClassMethodInvoke(ClientData clientData,
			    Tcl_Interp *interp, Tcl_ObjectContext context,
			    int objc, Tcl_Obj *const *objv);

/*
 * The types of methods defined by the core OO system.
 */

static const Tcl_MethodType procMethodType = {
    TCL_OO_METHOD_VERSION_CURRENT, "procedural method",
    InvokeProcedureMethod, DeleteProcedureMethod, CloneProcedureMethod
};
static const Tcl_MethodType fwdMethodType = {
    TCL_OO_METHOD_VERSION_CURRENT, "forward",
    InvokeForwardMethod, DeleteForwardMethod, CloneForwardMethod
};
static const Tcl_MethodType coreMethodType = {
    TCL_OO_METHOD_VERSION_CURRENT, "core method",
    BasicClassMethodInvoke, NULL, NULL
};

/*
 * ----------------------------------------------------------------------
 *
 * Tcl_NewMethod --
 *
 *	Attach a method to an object.
 *
 * ----------------------------------------------------------------------
 */

Tcl_Method
Tcl_NewMethod(
    Tcl_Interp *interp,		/* Unused? */
    Tcl_Object object,		/* The object that has the method attached to
				 * it. */
    Tcl_Obj *nameObj,		/* The name of the method. May be NULL; if so,
				 * up to caller to manage storage (e.g., when
				 * it is a constructor or destructor). */
    int flags,			/* Whether this is a public method. */
    const Tcl_MethodType *typePtr,
				/* The type of method this is, which defines
				 * how to invoke, delete and clone the
				 * method. */
    ClientData clientData)	/* Some data associated with the particular
				 * method to be created. */
{
    register Object *oPtr = (Object *) object;
    register Method *mPtr;
    Tcl_HashEntry *hPtr;
    int isNew;

    if (nameObj == NULL) {
	mPtr = (Method *) ckalloc(sizeof(Method));
	mPtr->namePtr = NULL;
	goto populate;
    }
    hPtr = Tcl_CreateHashEntry(&oPtr->methods, (char *) nameObj, &isNew);
    if (isNew) {
	mPtr = (Method *) ckalloc(sizeof(Method));
	mPtr->namePtr = nameObj;
	Tcl_IncrRefCount(nameObj);
	Tcl_SetHashValue(hPtr, mPtr);
    } else {
	mPtr = Tcl_GetHashValue(hPtr);
	if (mPtr->typePtr != NULL && mPtr->typePtr->deleteProc != NULL) {
	    mPtr->typePtr->deleteProc(mPtr->clientData);
	}
    }

  populate:
    mPtr->typePtr = typePtr;
    mPtr->clientData = clientData;
    mPtr->flags = 0;
    mPtr->declaringObjectPtr = oPtr;
    mPtr->declaringClassPtr = NULL;
    if (flags) {
	mPtr->flags |= flags & (PUBLIC_METHOD | PRIVATE_METHOD);
    }
    oPtr->epoch++;
    return (Tcl_Method) mPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * Tcl_NewClassMethod --
 *
 *	Attach a method to a class.
 *
 * ----------------------------------------------------------------------
 */

Tcl_Method
Tcl_NewClassMethod(
    Tcl_Interp *interp,		/* The interpreter containing the class. */
    Tcl_Class cls,		/* The class to attach the method to. */
    Tcl_Obj *nameObj,		/* The name of the object. May be NULL (e.g.,
				 * for constructors or destructors); if so, up
				 * to caller to manage storage. */
    int flags,			/* Whether this is a public method. */
    const Tcl_MethodType *typePtr,
				/* The type of method this is, which defines
				 * how to invoke, delete and clone the
				 * method. */
    ClientData clientData)	/* Some data associated with the particular
				 * method to be created. */
{
    register Class *clsPtr = (Class *) cls;
    register Method *mPtr;
    Tcl_HashEntry *hPtr;
    int isNew;

    if (nameObj == NULL) {
	mPtr = (Method *) ckalloc(sizeof(Method));
	mPtr->namePtr = NULL;
	goto populate;
    }
    hPtr = Tcl_CreateHashEntry(&clsPtr->classMethods, (char *)nameObj,&isNew);
    if (isNew) {
	mPtr = (Method *) ckalloc(sizeof(Method));
	mPtr->namePtr = nameObj;
	Tcl_IncrRefCount(nameObj);
	Tcl_SetHashValue(hPtr, mPtr);
    } else {
	mPtr = Tcl_GetHashValue(hPtr);
	if (mPtr->typePtr != NULL && mPtr->typePtr->deleteProc != NULL) {
	    mPtr->typePtr->deleteProc(mPtr->clientData);
	}
    }

  populate:
    TclOOGetFoundation(interp)->epoch++;
    mPtr->typePtr = typePtr;
    mPtr->clientData = clientData;
    mPtr->flags = 0;
    mPtr->declaringObjectPtr = NULL;
    mPtr->declaringClassPtr = clsPtr;
    if (flags) {
	mPtr->flags |= flags & (PUBLIC_METHOD | PRIVATE_METHOD);
    }

    return (Tcl_Method) mPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteMethodStruct --
 *
 *	Function used when deleting a method. Always called indirectly via
 *	Tcl_EventuallyFree().
 *
 * ----------------------------------------------------------------------
 */

static void
DeleteMethodStruct(
    char *buffer)
{
    Method *mPtr = (Method *) buffer;

    if (mPtr->typePtr != NULL && mPtr->typePtr->deleteProc != NULL) {
	mPtr->typePtr->deleteProc(mPtr->clientData);
    }
    if (mPtr->namePtr != NULL) {
	Tcl_DecrRefCount(mPtr->namePtr);
    }

    ckfree(buffer);
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOODeleteMethod --
 *
 *	How to delete a method.
 *
 * ----------------------------------------------------------------------
 */

void
TclOODeleteMethod(
    Method *mPtr)
{
    if (mPtr != NULL) {
	Tcl_EventuallyFree(mPtr, DeleteMethodStruct);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * DeclareClassMethod --
 *
 *	Helper that makes it cleaner to create very simple methods during
 *	basic system initialization. Not suitable for general use.
 *
 * ----------------------------------------------------------------------
 */

void
TclOONewBasicClassMethod(
    Tcl_Interp *interp,
    Class *clsPtr,		/* Class to attach the method to. */
    const DeclaredClassMethod *dcm)
				/* Name of the method, whether it is public,
				 * and the function to implement it. */
{
    Tcl_Obj *namePtr = Tcl_NewStringObj(dcm->name, -1);

    Tcl_IncrRefCount(namePtr);
    Tcl_NewClassMethod(interp, (Tcl_Class) clsPtr, namePtr,
	    (dcm->isPublic ? PUBLIC_METHOD : 0), &coreMethodType,
	    (ClientData) dcm);
    Tcl_DecrRefCount(namePtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * BasicClassMethodInvoke --
 *
 *	How to invoke a simple method.
 *
 * ----------------------------------------------------------------------
 */

static int
BasicClassMethodInvoke(
    ClientData clientData,	/* Pointer to function that implements the
				 * method. */
    Tcl_Interp *interp,
    Tcl_ObjectContext context,	/* The method calling context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* Arguments as actually seen. */
{
    const DeclaredClassMethod *dcm = clientData;

    return (dcm->callProc)(NULL, interp, context, objc, objv);
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOONewProcMethod --
 *
 *	Create a new procedure-like method for an object.
 *
 * ----------------------------------------------------------------------
 */

Method *
TclOONewProcMethod(
    Tcl_Interp *interp,		/* The interpreter containing the object. */
    Object *oPtr,		/* The object to modify. */
    int flags,			/* Whether this is a public method. */
    Tcl_Obj *nameObj,		/* The name of the method, which must not be
				 * NULL. */
    Tcl_Obj *argsObj,		/* The formal argument list for the method,
				 * which must not be NULL. */
    Tcl_Obj *bodyObj)		/* The body of the method, which must not be
				 * NULL. */
{
    Interp *iPtr = (Interp *) interp;
    int argsc;
    Tcl_Obj **argsv;
    register ProcedureMethod *pmPtr;
    const char *procName;

    if (Tcl_ListObjGetElements(interp, argsObj, &argsc, &argsv) != TCL_OK) {
	return NULL;
    }
    pmPtr = (ProcedureMethod *) ckalloc(sizeof(ProcedureMethod));
    procName = TclGetString(nameObj);
    if (TclCreateProc(interp, NULL, procName, argsObj, bodyObj,
	    &pmPtr->procPtr) != TCL_OK) {
	ckfree((char *) pmPtr);
	return NULL;
    }
    pmPtr->procPtr->cmdPtr = NULL;

    if (iPtr->cmdFramePtr) {
	CmdFrame context = *iPtr->cmdFramePtr;

	if (context.type == TCL_LOCATION_BC) {
	    /*
	     * Retrieve source information from the bytecode, if possible. If
	     * the information is retrieved successfully, context.type will be
	     * TCL_LOCATION_SOURCE and the reference held by
	     * context.data.eval.path will be counted.
	     */

	    TclGetSrcInfoForPc(&context);
	} else if (context.type == TCL_LOCATION_SOURCE) {
	    /*
	     * The copy into 'context' up above has created another reference
	     * to 'context.data.eval.path'; account for it.
	     */

	    Tcl_IncrRefCount(context.data.eval.path);
	}

	if (context.type == TCL_LOCATION_SOURCE) {
	    /*
	     * We can account for source location within a proc only if the
	     * proc body was not created by substitution.
	     */

	    if (context.line
		    && (context.nline >= 4) && (context.line[3] >= 0)) {
		int isNew;
		CmdFrame *cfPtr = (CmdFrame *) ckalloc(sizeof(CmdFrame));
		Tcl_HashEntry *hPtr;

		cfPtr->level = -1;
		cfPtr->type = context.type;
		cfPtr->line = (int *) ckalloc(sizeof(int));
		cfPtr->line[0] = context.line[3];
		cfPtr->nline = 1;
		cfPtr->framePtr = NULL;
		cfPtr->nextPtr = NULL;

		cfPtr->data.eval.path = context.data.eval.path;
		Tcl_IncrRefCount(cfPtr->data.eval.path);

		cfPtr->cmd.str.cmd = NULL;
		cfPtr->cmd.str.len = 0;

		hPtr = Tcl_CreateHashEntry(iPtr->linePBodyPtr,
			(char *) pmPtr->procPtr, &isNew);
		Tcl_SetHashValue(hPtr, cfPtr);
	    }

	    /*
	     * 'context' is going out of scope; account for the reference that
	     * it's holding to the path name.
	     */

	    Tcl_DecrRefCount(context.data.eval.path);
	    context.data.eval.path = NULL;
	}
    }

    return (Method *) Tcl_NewMethod(interp, (Tcl_Object) oPtr, nameObj,
	    flags, &procMethodType, pmPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOONewProcClassMethod --
 *
 *	Create a new procedure-like method for a class.
 *
 * ----------------------------------------------------------------------
 */

Method *
TclOONewProcClassMethod(
    Tcl_Interp *interp,		/* The interpreter containing the class. */
    Class *clsPtr,		/* The class to modify. */
    int flags,			/* Whether this is a public method. */
    Tcl_Obj *nameObj,		/* The name of the method, which may be NULL;
				 * if so, up to caller to manage storage
				 * (e.g., because it is a constructor or
				 * destructor). */
    Tcl_Obj *argsObj,		/* The formal argument list for the method,
				 * which may be NULL; if so, it is equivalent
				 * to an empty list. */
    Tcl_Obj *bodyObj)		/* The body of the method, which must not be
				 * NULL. */
{
    Interp *iPtr = (Interp *) interp;
    int argsLen;		/* -1 => delete argsObj before exit */
    register ProcedureMethod *pmPtr;
    const char *procName;

    if (argsObj == NULL) {
	argsLen = -1;
	argsObj = Tcl_NewObj();
	Tcl_IncrRefCount(argsObj);
	procName = "<destructor>";
    } else if (Tcl_ListObjLength(interp, argsObj, &argsLen) != TCL_OK) {
	return NULL;
    } else {
	procName = (nameObj==NULL ? "<constructor>" : TclGetString(nameObj));
    }
    pmPtr = (ProcedureMethod *) ckalloc(sizeof(ProcedureMethod));
    if (TclCreateProc(interp, NULL, procName, argsObj, bodyObj,
	    &pmPtr->procPtr) != TCL_OK) {
	if (argsLen == -1) {
	    Tcl_DecrRefCount(argsObj);
	}
	ckfree((char *) pmPtr);
	return NULL;
    }
    pmPtr->procPtr->cmdPtr = NULL;
    if (argsLen == -1) {
	Tcl_DecrRefCount(argsObj);
    }

    if (iPtr->cmdFramePtr) {
	CmdFrame context = *iPtr->cmdFramePtr;

	if (context.type == TCL_LOCATION_BC) {
	    /*
	     * Retrieve source information from the bytecode, if possible. If
	     * the information is retrieved successfully, context.type will be
	     * TCL_LOCATION_SOURCE and the reference held by
	     * context.data.eval.path will be counted.
	     */

	    TclGetSrcInfoForPc(&context);
	} else if (context.type == TCL_LOCATION_SOURCE) {
	    /*
	     * The copy into 'context' up above has created another reference
	     * to 'context.data.eval.path'; account for it.
	     */

	    Tcl_IncrRefCount(context.data.eval.path);
	}

	if (context.type == TCL_LOCATION_SOURCE) {
	    /*
	     * We can account for source location within a proc only if the
	     * proc body was not created by substitution.
	     */

	    if (context.line
		    && (context.nline >= 4) && (context.line[3] >= 0)) {
		int isNew;
		CmdFrame *cfPtr = (CmdFrame *) ckalloc(sizeof(CmdFrame));
		Tcl_HashEntry *hPtr;

		cfPtr->level = -1;
		cfPtr->type = context.type;
		cfPtr->line = (int *) ckalloc(sizeof(int));
		cfPtr->line[0] = context.line[3];
		cfPtr->nline = 1;
		cfPtr->framePtr = NULL;
		cfPtr->nextPtr = NULL;

		cfPtr->data.eval.path = context.data.eval.path;
		Tcl_IncrRefCount(cfPtr->data.eval.path);

		cfPtr->cmd.str.cmd = NULL;
		cfPtr->cmd.str.len = 0;

		hPtr = Tcl_CreateHashEntry(iPtr->linePBodyPtr,
			(char *) pmPtr->procPtr, &isNew);
		Tcl_SetHashValue(hPtr, cfPtr);
	    }

	    /*
	     * 'context' is going out of scope; account for the reference that
	     * it's holding to the path name.
	     */

	    Tcl_DecrRefCount(context.data.eval.path);
	    context.data.eval.path = NULL;
	}
    }

    return (Method *) Tcl_NewClassMethod(interp, (Tcl_Class) clsPtr, nameObj,
	    flags, &procMethodType, pmPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * InvokeProcedureMethod --
 *
 *	How to invoke a procedure-like method.
 *
 * ----------------------------------------------------------------------
 */

static int
InvokeProcedureMethod(
    ClientData clientData,	/* Pointer to some per-method context. */
    Tcl_Interp *interp,
    Tcl_ObjectContext context,	/* The method calling context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* Arguments as actually seen. */
{
    CallContext *contextPtr = (CallContext *) context;
    ProcedureMethod *pmPtr = clientData;
    int result, flags = FRAME_IS_METHOD, skip = contextPtr->skip;
    CallFrame *framePtr, **framePtrPtr;
    Object *oPtr = contextPtr->oPtr;
    Command cmd;
    const char *namePtr;
    Tcl_Obj *nameObj;
    void (*errProc)(Tcl_Interp *,Tcl_Obj *);
    ExtraFrameInfo efi;
    struct PNI pni;

    efi.length = 2;
    memset(&cmd, 0, sizeof(Command));
    cmd.nsPtr = (Namespace *) oPtr->namespacePtr;
    cmd.clientData = &efi;
    pmPtr->procPtr->cmdPtr = &cmd;
    if (contextPtr->flags & CONSTRUCTOR) {
	Foundation *fPtr = TclOOGetFoundation(interp);

	namePtr = "<constructor>";
	flags |= FRAME_IS_CONSTRUCTOR;
	nameObj = fPtr->constructorName;
	errProc = ConstructorErrorHandler;
    } else if (contextPtr->flags & DESTRUCTOR) {
	Foundation *fPtr = TclOOGetFoundation(interp);

	namePtr = "<destructor>";
	flags |= FRAME_IS_DESTRUCTOR;
	nameObj = fPtr->destructorName;
	errProc = DestructorErrorHandler;
    } else {
	nameObj = Tcl_MethodName(Tcl_ObjectContextMethod(context));
	namePtr = TclGetString(nameObj);
	errProc = MethodErrorHandler;
    }
    result = TclProcCompileProc(interp, pmPtr->procPtr,
	    pmPtr->procPtr->bodyPtr, (Namespace *) oPtr->namespacePtr,
	    "body of method", namePtr);
    if (result != TCL_OK) {
	return result;
    }

    if (contextPtr->callChain[contextPtr->index].isFilter) {
	flags |= FRAME_IS_FILTER;
    }
    flags |= FRAME_IS_PROC;
    framePtrPtr = &framePtr;
    result = TclPushStackFrame(interp, (Tcl_CallFrame **) framePtrPtr,
	    oPtr->namespacePtr, flags);
    if (result != TCL_OK) {
	return result;
    }

    framePtr->clientData = contextPtr;
    framePtr->objc = objc;
    framePtr->objv = objv;	/* ref counts for args are incremented below */
    framePtr->procPtr = pmPtr->procPtr;

    /*
     * Finish filling out the extra frame info.
     */

    efi.fields[0].name = "method";
    efi.fields[0].proc = NULL;
    efi.fields[0].clientData = nameObj;
    pni.interp = interp;
    pni.method = Tcl_ObjectContextMethod(context);
    efi.fields[1].proc = RenderDeclarerName;
    efi.fields[1].clientData = &pni;
    if (Tcl_MethodDeclarerObject(pni.method) != NULL) {
	efi.fields[1].name = "object";
    } else {
	efi.fields[1].name = "class";
    }

    /*
     * Ensure that the method name itself is part of the arguments when we're
     * doing unknown processing.
     */

    if (contextPtr->flags & OO_UNKNOWN_METHOD) {
	skip--;
    }

    /*
     * Now invoke the body of the method.
     */

    result = TclObjInterpProcCore(interp, nameObj, skip, errProc);
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * RenderDeclarerName --
 *
 *	Returns the name of the entity (object or class) which declared a
 *	method. Used for producing information for [info frame] in such a way
 *	that the expensive part of this (generating the object or class name
 *	itself) isn't done until it is needed.
 *
 * ----------------------------------------------------------------------
 */

static Tcl_Obj *
RenderDeclarerName(
    ClientData clientData)
{
    struct PNI *pni = clientData;
    Tcl_Object object = Tcl_MethodDeclarerObject(pni->method);

    if (object == NULL) {
	object = Tcl_GetClassAsObject(Tcl_MethodDeclarerClass(pni->method));
    }
    return TclOOObjectName(pni->interp, (Object *) object);
}

/*
 * ----------------------------------------------------------------------
 *
 * MethodErrorHandler, ConstructorErrorHandler, DestructorErrorHandler --
 *
 *	How to fill in the stack trace correctly upon error in various forms
 *	of procedure-like methods. LIMIT is how long the inserted strings in
 *	the error traces should get before being converted to have ellipses,
 *	and ELLIPSIFY is a macro to do the conversion (with the help of a
 *	%.*s%s format field). Note that ELLIPSIFY is only safe for use in
 *	suitable formatting contexts.
 *
 * ----------------------------------------------------------------------
 */

#define LIMIT 60
#define ELLIPSIFY(str,len) \
	((len) > LIMIT ? LIMIT : (len)), (str), ((len) > LIMIT ? "..." : "")

static void
MethodErrorHandler(
    Tcl_Interp *interp,
    Tcl_Obj *methodNameObj)
{
    int nameLen, objectNameLen;
    CallContext *contextPtr = ((Interp *) interp)->varFramePtr->clientData;
    Method *mPtr = contextPtr->callChain[contextPtr->index].mPtr;
    const char *objectName, *kindName, *methodName =
	    Tcl_GetStringFromObj(mPtr->namePtr, &nameLen);
    Object *declarerPtr;

    if (mPtr->declaringObjectPtr != NULL) {
	declarerPtr = mPtr->declaringObjectPtr;
	kindName = "object";
    } else {
	if (mPtr->declaringClassPtr == NULL) {
	    Tcl_Panic("method not declared in class or object");
	}
	declarerPtr = mPtr->declaringClassPtr->thisPtr;
	kindName = "class";
    }

    objectName = Tcl_GetStringFromObj(TclOOObjectName(interp, declarerPtr),
	    &objectNameLen);
    Tcl_AppendObjToErrorInfo(interp, Tcl_ObjPrintf(
	    "\n    (%s \"%.*s%s\" method \"%.*s%s\" line %d)",
	    kindName, ELLIPSIFY(objectName, objectNameLen),
	    ELLIPSIFY(methodName, nameLen), interp->errorLine));
}

static void
ConstructorErrorHandler(
    Tcl_Interp *interp,
    Tcl_Obj *methodNameObj)
{
    CallContext *contextPtr = ((Interp *) interp)->varFramePtr->clientData;
    Method *mPtr = contextPtr->callChain[contextPtr->index].mPtr;
    Object *declarerPtr;
    const char *objectName, *kindName;
    int objectNameLen;

    if (interp->errorLine == 0xDEADBEEF) {
	/*
	 * Horrible hack to deal with certain constructors that must not add
	 * information to the error trace.
	 */

	return;
    }

    if (mPtr->declaringObjectPtr != NULL) {
	declarerPtr = mPtr->declaringObjectPtr;
	kindName = "object";
    } else {
	if (mPtr->declaringClassPtr == NULL) {
	    Tcl_Panic("method not declared in class or object");
	}
	declarerPtr = mPtr->declaringClassPtr->thisPtr;
	kindName = "class";
    }

    objectName = Tcl_GetStringFromObj(TclOOObjectName(interp, declarerPtr),
	    &objectNameLen);
    Tcl_AppendObjToErrorInfo(interp, Tcl_ObjPrintf(
	    "\n    (%s \"%.*s%s\" constructor line %d)", kindName,
	    ELLIPSIFY(objectName, objectNameLen), interp->errorLine));
}

static void
DestructorErrorHandler(
    Tcl_Interp *interp,
    Tcl_Obj *methodNameObj)
{
    CallContext *contextPtr = ((Interp *) interp)->varFramePtr->clientData;
    Method *mPtr = contextPtr->callChain[contextPtr->index].mPtr;
    Object *declarerPtr;
    const char *objectName, *kindName;
    int objectNameLen;

    if (mPtr->declaringObjectPtr != NULL) {
	declarerPtr = mPtr->declaringObjectPtr;
	kindName = "object";
    } else {
	if (mPtr->declaringClassPtr == NULL) {
	    Tcl_Panic("method not declared in class or object");
	}
	declarerPtr = mPtr->declaringClassPtr->thisPtr;
	kindName = "class";
    }

    objectName = Tcl_GetStringFromObj(TclOOObjectName(interp, declarerPtr),
	    &objectNameLen);
    Tcl_AppendObjToErrorInfo(interp, Tcl_ObjPrintf(
	    "\n    (%s \"%.*s%s\" destructor line %d)", kindName,
	    ELLIPSIFY(objectName, objectNameLen), interp->errorLine));
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteProcedureMethod, CloneProcedureMethod --
 *
 *	How to delete and clone procedure-like methods.
 *
 * ----------------------------------------------------------------------
 */

static void
DeleteProcedureMethod(
    ClientData clientData)
{
    register ProcedureMethod *pmPtr = clientData;

    TclProcDeleteProc(pmPtr->procPtr);
    ckfree((char *) pmPtr);
}

static int
CloneProcedureMethod(
    ClientData clientData,
    ClientData *newClientData)
{
    ProcedureMethod *pmPtr = clientData;
    ProcedureMethod *pm2Ptr = (ProcedureMethod *)
	    ckalloc(sizeof(ProcedureMethod));

    pm2Ptr->procPtr = pmPtr->procPtr;
    pm2Ptr->procPtr->refCount++;
    *newClientData = pm2Ptr;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOONewForwardMethod --
 *
 *	Create a forwarded method for an object.
 *
 * ----------------------------------------------------------------------
 */

Method *
TclOONewForwardMethod(
    Tcl_Interp *interp,		/* Interpreter for error reporting. */
    Object *oPtr,		/* The object to attach the method to. */
    int flags,			/* Whether the method is public or not. */
    Tcl_Obj *nameObj,		/* The name of the method. */
    Tcl_Obj *prefixObj)		/* List of arguments that form the command
				 * prefix to forward to. */
{
    int prefixLen;
    register ForwardMethod *fmPtr;

    if (Tcl_ListObjLength(interp, prefixObj, &prefixLen) != TCL_OK) {
	return NULL;
    }
    if (prefixLen < 1) {
	Tcl_AppendResult(interp, "method forward prefix must be non-empty",
		NULL);
	return NULL;
    }

    fmPtr = (ForwardMethod *) ckalloc(sizeof(ForwardMethod));
    fmPtr->prefixObj = prefixObj;
    Tcl_IncrRefCount(prefixObj);
    return (Method *) Tcl_NewMethod(interp, (Tcl_Object) oPtr, nameObj,
	    flags, &fwdMethodType, fmPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOONewForwardClassMethod --
 *
 *	Create a new forwarded method for a class.
 *
 * ----------------------------------------------------------------------
 */

Method *
TclOONewForwardClassMethod(
    Tcl_Interp *interp,		/* Interpreter for error reporting. */
    Class *clsPtr,		/* The class to attach the method to. */
    int flags,			/* Whether the method is public or not. */
    Tcl_Obj *nameObj,		/* The name of the method. */
    Tcl_Obj *prefixObj)		/* List of arguments that form the command
				 * prefix to forward to. */
{
    int prefixLen;
    register ForwardMethod *fmPtr;

    if (Tcl_ListObjLength(interp, prefixObj, &prefixLen) != TCL_OK) {
	return NULL;
    }
    if (prefixLen < 1) {
	Tcl_AppendResult(interp, "method forward prefix must be non-empty",
		NULL);
	return NULL;
    }

    fmPtr = (ForwardMethod *) ckalloc(sizeof(ForwardMethod));
    fmPtr->prefixObj = prefixObj;
    Tcl_IncrRefCount(prefixObj);
    return (Method *) Tcl_NewClassMethod(interp, (Tcl_Class) clsPtr, nameObj,
	    flags, &fwdMethodType, fmPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * InvokeForwardMethod --
 *
 *	How to invoke a forwarded method. Works by doing some ensemble-like
 *	command rearranging and then invokes some other Tcl command.
 *
 * ----------------------------------------------------------------------
 */

static int
InvokeForwardMethod(
    ClientData clientData,	/* Pointer to some per-method context. */
    Tcl_Interp *interp,
    Tcl_ObjectContext context,	/* The method calling context. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const *objv)	/* Arguments as actually seen. */
{
    CallContext *contextPtr = (CallContext *) context;
    ForwardMethod *fmPtr = clientData;
    Tcl_Obj **argObjs, **prefixObjs;
    int numPrefixes, result, len, skip = contextPtr->skip;

    /*
     * Ensure that the method name itself is part of the arguments when we're
     * doing unknown processing.
     */

    if (contextPtr->flags & OO_UNKNOWN_METHOD) {
	skip--;
    }

    /*
     * Build the real list of arguments to use. Note that we know that the
     * prefixObj field of the ForwardMethod structure holds a reference to a
     * non-empty list, so there's a whole class of failures ("not a list") we
     * can ignore here.
     */

    TclListObjGetElements(fmPtr->prefixObj, numPrefixes, prefixObjs);
    argObjs = InitEnsembleRewrite(interp, objc, objv, skip,
	    numPrefixes, prefixObjs, &len);

    result = Tcl_EvalObjv(interp, len, argObjs, TCL_EVAL_INVOKE);
    ckfree((char *) argObjs);
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteForwardMethod, CloneForwardMethod --
 *
 *	How to delete and clone forwarded methods.
 *
 * ----------------------------------------------------------------------
 */

static void
DeleteForwardMethod(
    ClientData clientData)
{
    ForwardMethod *fmPtr = clientData;

    Tcl_DecrRefCount(fmPtr->prefixObj);
    ckfree((char *) fmPtr);
}

static int
CloneForwardMethod(
    ClientData clientData,
    ClientData *newClientData)
{
    ForwardMethod *fmPtr = clientData;
    ForwardMethod *fm2Ptr = (ForwardMethod *) ckalloc(sizeof(ForwardMethod));

    fm2Ptr->prefixObj = fmPtr->prefixObj;
    Tcl_IncrRefCount(fm2Ptr->prefixObj);
    *newClientData = fm2Ptr;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TclOOGetProcFromMethod, TclOOGetFwdFromMethod --
 *
 *	Utility functions used for procedure-like and forwarding method
 *	introspection.
 *
 * ----------------------------------------------------------------------
 */

Proc *
TclOOGetProcFromMethod(
    Method *mPtr)
{
    if (mPtr->typePtr == &procMethodType) {
	ProcedureMethod *pmPtr = mPtr->clientData;

	return pmPtr->procPtr;
    }
    return NULL;
}

Tcl_Obj *
TclOOGetFwdFromMethod(
    Method *mPtr)
{
    if (mPtr->typePtr == &fwdMethodType) {
	ForwardMethod *fwPtr = mPtr->clientData;

	return fwPtr->prefixObj;
    }
    return NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * InitEnsembleRewrite --
 *
 *	Utility function that wraps up a lot of the complexity involved in
 *	doing ensemble-like command forwarding. Here is a picture of memory
 *	management plan:
 *
 *                    <-----------------objc---------------------->
 *      objv:        |=============|===============================|
 *                    <-toRewrite->           |
 *                                             \
 *                    <-rewriteLength->         \
 *      rewriteObjs: |=================|         \
 *                           |                    |
 *                           V                    V
 *      argObjs:     |=================|===============================|
 *                    <------------------*lengthPtr------------------->
 *
 * ----------------------------------------------------------------------
 */

static Tcl_Obj **
InitEnsembleRewrite(
    Tcl_Interp *interp,		/* Place to log the rewrite info. */
    int objc,			/* Number of real arguments. */
    Tcl_Obj *const *objv,	/* The real arguments. */
    int toRewrite,		/* Number of real arguments to replace. */
    int rewriteLength,		/* Number of arguments to insert instead. */
    Tcl_Obj *const *rewriteObjs,/* Arguments to insert instead. */
    int *lengthPtr)		/* Where to write the resulting length of the
				 * array of rewritten arguments. */
{
    Interp *iPtr = (Interp *) interp;
    int isRootEnsemble = (iPtr->ensembleRewrite.sourceObjs == NULL);
    Tcl_Obj **argObjs;
    unsigned len = rewriteLength + objc - toRewrite;

    argObjs = (Tcl_Obj **) ckalloc(sizeof(Tcl_Obj *) * len);
    memcpy(argObjs, rewriteObjs, rewriteLength * sizeof(Tcl_Obj *));
    memcpy(argObjs + rewriteLength, objv + toRewrite,
	    sizeof(Tcl_Obj *) * (objc - toRewrite));

    /*
     * Now plumb this into the core ensemble rewrite logging system so that
     * Tcl_WrongNumArgs() can rewrite its result appropriately. The rules for
     * how to store the rewrite rules get complex solely because of the case
     * where an ensemble rewrites itself out of the picture; when that
     * happens, the quality of the error message rewrite falls drastically
     * (and unavoidably).
     */

    if (isRootEnsemble) {
	iPtr->ensembleRewrite.sourceObjs = objv;
	iPtr->ensembleRewrite.numRemovedObjs = toRewrite;
	iPtr->ensembleRewrite.numInsertedObjs = rewriteLength;
    } else {
	int numIns = iPtr->ensembleRewrite.numInsertedObjs;

	if (numIns < toRewrite) {
	    iPtr->ensembleRewrite.numRemovedObjs += toRewrite - numIns;
	    iPtr->ensembleRewrite.numInsertedObjs += rewriteLength - 1;
	} else {
	    iPtr->ensembleRewrite.numInsertedObjs +=
		    rewriteLength - toRewrite;
	}
    }

    *lengthPtr = len;
    return argObjs;
}

/*
 * ----------------------------------------------------------------------
 *
 * assorted trivial 'getter' functions
 *
 * ----------------------------------------------------------------------
 */

Tcl_Object
Tcl_MethodDeclarerObject(
    Tcl_Method method)
{
    return (Tcl_Object) ((Method *) method)->declaringObjectPtr;
}

Tcl_Class
Tcl_MethodDeclarerClass(
    Tcl_Method method)
{
    return (Tcl_Class) ((Method *) method)->declaringClassPtr;
}

Tcl_Obj *
Tcl_MethodName(
    Tcl_Method method)
{
    return ((Method *) method)->namePtr;
}

int
Tcl_MethodIsType(
    Tcl_Method method,
    const Tcl_MethodType *typePtr,
    ClientData *clientDataPtr)
{
    Method *mPtr = (Method *) method;

    if (mPtr->typePtr == typePtr) {
	if (clientDataPtr != NULL) {
	    *clientDataPtr = mPtr->clientData;
	}
	return 1;
    }
    return 0;
}

int
Tcl_MethodIsPublic(
    Tcl_Method method)
{
    return (((Method *)method)->flags & PUBLIC_METHOD) ? 1 : 0;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
