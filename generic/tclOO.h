/*
 * tclOOInt.h --
 *
 *	This file contains the public API definitions and some of the function
 *	declarations for the object-system (NB: not Tcl_Obj, but ::oo).
 *
 * Copyright (c) 2006 by Donal K. Fellows
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tclOO.h,v 1.1 2007/05/18 13:17:15 dkf Exp $
 */

#ifndef TCLOO_H_INCLUDED
#define TCLOO_H_INCLUDED
#include "tcl.h"

#if defined(BUILD_tcloo)
#	define TCLOOAPI DLLEXPORT
#	undef USE_TCLOO_STUBS
#else
#	define TCLOOAPI DLLIMPORT
#endif

#define OO_VERSION "0.1"
#define OO_PATCHLEVEL OO_VERSION

typedef struct Tcl_Class_ *Tcl_Class;
typedef struct Tcl_Method_ *Tcl_Method;
typedef struct Tcl_Object_ *Tcl_Object;
typedef struct Tcl_ObjectContext_ *Tcl_ObjectContext;

/*
 * Public datatypes for callbacks and structures used in the TIP#257 (OO)
 * implementation. These are used to implement custom types of method calls
 * and to allow the attachment of arbitrary data to objects and classes.
 */

typedef int (*Tcl_MethodCallProc)_ANSI_ARGS_((ClientData clientData,
	Tcl_Interp *interp, Tcl_ObjectContext objectContext, int objc,
	Tcl_Obj *const *objv));
typedef void (*Tcl_MethodDeleteProc)_ANSI_ARGS_((ClientData clientData));
typedef int (*Tcl_MethodCloneProc)_ANSI_ARGS_((ClientData oldClientData,
	ClientData *newClientData));
typedef void (*Tcl_ObjectMetadataDeleteProc)_ANSI_ARGS_((
	ClientData clientData));
typedef ClientData (*Tcl_ObjectMetadataCloneProc)_ANSI_ARGS_((
	ClientData clientData));

/*
 * The type of a method implementation. This describes how to call the method
 * implementation, how to delete it (when the object or class is deleted) and
 * how to create a clone of it (when the object or class is copied).
 */

typedef struct {
    int version;		/* Structure version field. Always to be equal
				 * to TCL_OO_METHOD_VERSION_CURRENT in
				 * declarations. */
    const char *name;		/* Name of this type of method, mostly for
				 * debugging purposes. */
    Tcl_MethodCallProc callProc;/* How to invoke this method. */
    Tcl_MethodDeleteProc deleteProc;
				/* How to delete this method's type-specific
				 * data, or NULL if the type-specific data
				 * does not need deleting. */
    Tcl_MethodCloneProc cloneProc;
				/* How to copy this method's type-specific
				 * data, or NULL if the type-specific data can
				 * be copied directly. */
} Tcl_MethodType;

/*
 * The correct value for the version field of the Tcl_MethodType structure.
 * This allows new versions of the structure to be introduced without breaking
 * binary compatability.
 */

#define TCL_OO_METHOD_VERSION_CURRENT 1

/*
 * The type of some object (or class) metadata. This describes how to delete
 * the metadata (when the object or class is deleted) and how to create a
 * clone of it (when the object or class is copied).
 */

typedef struct {
    int version;		/* Structure version field. Always to be equal
				 * to TCL_OO_METADATA_VERSION_CURRENT in
				 * declarations. */
    const char *name;
    Tcl_ObjectMetadataDeleteProc deleteProc;
				/* How to delete the metadata. This must not
				 * be NULL. */
    Tcl_ObjectMetadataCloneProc cloneProc;
				/* How to clone the metadata. If NULL, the
				 * metadata will not be copied. */
} Tcl_ObjectMetadataType;

/*
 * The correct value for the version field of the Tcl_ObjectMetadataType
 * structure. This allows new versions of the structure to be introduced
 * without breaking binary compatability.
 */

#define TCL_OO_METADATA_VERSION_CURRENT 1

/*
// vvvvvvvvvvvvvvvvvvvvvv MOVE TO OO.DECLS vvvvvvvvvvvvvvvvvvvvvv
Tcl_Object		Tcl_CopyObjectInstance(Tcl_Interp *interp,
			    Tcl_Object sourceObject, const char *targetName);
Tcl_Object		Tcl_GetClassAsObject(Tcl_Class clazz);
Tcl_Class		Tcl_GetObjectAsClass(Tcl_Object object);
Tcl_Command		Tcl_GetObjectCommand(Tcl_Object object);
Tcl_Object		Tcl_GetObjectFromObj(Tcl_Interp *interp,
			    Tcl_Obj *objPtr);
Tcl_Namespace *		Tcl_GetObjectNamespace(Tcl_Object object);
Tcl_Class		Tcl_MethodDeclarerClass(Tcl_Method method);
Tcl_Object		Tcl_MethodDeclarerObject(Tcl_Method method);
int			Tcl_MethodIsPublic(Tcl_Method method);
int			Tcl_MethodIsType(Tcl_Method method,
			    const Tcl_MethodType *typePtr,
			    ClientData *clientDataPtr);
Tcl_Obj *		Tcl_MethodName(Tcl_Method method);
Tcl_Method		Tcl_NewMethod(Tcl_Interp *interp, Tcl_Object object,
			    Tcl_Obj *nameObj, int isPublic,
			    const Tcl_MethodType *typePtr,
			    ClientData clientData);
Tcl_Method		Tcl_NewClassMethod(Tcl_Interp *interp, Tcl_Class cls,
			    Tcl_Obj *nameObj, int isPublic,
			    const Tcl_MethodType *typePtr,
			    ClientData clientData);
Tcl_Object		Tcl_NewObjectInstance(Tcl_Interp *interp,
			    Tcl_Class cls, const char *name, int objc,
			    Tcl_Obj *const *objv, int skip);
int			Tcl_ObjectDeleted(Tcl_Object object);
int			Tcl_ObjectContextIsFiltering(
			    Tcl_ObjectContext context);
Tcl_Method		Tcl_ObjectContextMethod(Tcl_ObjectContext context);
Tcl_Object		Tcl_ObjectContextObject(Tcl_ObjectContext context);
int			Tcl_ObjectContextSkippedArgs(
			    Tcl_ObjectContext context);
ClientData		Tcl_ClassGetMetadata(Tcl_Class clazz,
			    const Tcl_ObjectMetadataType *typePtr);
void			Tcl_ClassSetMetadata(Tcl_Class clazz,
			    const Tcl_ObjectMetadataType *typePtr,
			    ClientData metadata);
ClientData		Tcl_ObjectGetMetadata(Tcl_Object object,
			    const Tcl_ObjectMetadataType *typePtr);
void			Tcl_ObjectSetMetadata(Tcl_Object object,
			    const Tcl_ObjectMetadataType *typePtr,
			    ClientData metadata);
// ^^^^^^^^^^^^^^^^^^^^^^ MOVE TO OO.DECLS ^^^^^^^^^^^^^^^^^^^^^^
*/
#include "tclOODecls.h"

#endif

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
