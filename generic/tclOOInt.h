/*
 * tclOOInt.h --
 *
 *	This file contains the structure definitions and some of the function
 *	declarations for the object-system (NB: not Tcl_Obj, but ::oo).
 *
 * Copyright (c) 2006 by Donal K. Fellows
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tclOOInt.h,v 1.15 2008/01/03 14:41:57 dkf Exp $
 */

#include <tclInt.h>
#include "tclOO.h"

/*
 * Forward declarations.
 */

struct Class;
struct Object;

/*
 * The data that needs to be stored per method. This record is used to collect
 * information about all sorts of methods, including forwards, constructors
 * and destructors.
 */

typedef struct Method {
    const Tcl_MethodType *typePtr;
				/* The type of method. If NULL, this is a
				 * special flag record which is just used for
				 * the setting of the flags field. */
    ClientData clientData;	/* Type-specific data. */
    Tcl_Obj *namePtr;		/* Name of the method. */
    struct Object *declaringObjectPtr;
				/* The object that declares this method, or
				 * NULL if it was declared by a class. */
    struct Class *declaringClassPtr;
				/* The class that declares this method, or
				 * NULL if it was declared directly on an
				 * object. */
    int flags;			/* Assorted flags. Includes whether this
				 * method is public/exported or not. */
} Method;

/*
 * Pre- and post-call callbacks, to allow procedure-like methods to be fine
 * tuned in their behaviour.
 */

typedef int (*TclOO_PreCallProc)(ClientData clientData, Tcl_Interp *interp,
	Tcl_ObjectContext context, Tcl_CallFrame *framePtr, int *isFinished);
typedef int (*TclOO_PostCallProc)(ClientData clientData, Tcl_Interp *interp,
	Tcl_ObjectContext context, Tcl_Namespace *namespacePtr, int result);
typedef void (*TclOO_PmCDDeleteProc)(ClientData clientData);
typedef ClientData (*TclOO_PmCDCloneProc)(ClientData clientData);

/*
 * Procedure-like methods have the following extra information. It is a
 * single-field structure because this allows for future expansion without
 * changing vast amounts of code.
 */

typedef struct ProcedureMethod {
    int version;		/* Version of this structure. Currently must
				 * be 0. */
    Proc *procPtr;		/* Core of the implementation of the method;
				 * includes the argument definition and the
				 * body bytecodes. */
    int flags;			/* Flags to control features. */
    ClientData clientData;
    TclOO_PmCDDeleteProc deleteClientdataProc;
    TclOO_PmCDCloneProc cloneClientdataProc;
    ProcErrorProc errProc;	/* Replacement error handler. */
    TclOO_PreCallProc preCallProc;
				/* Callback to allow for additional setup
				 * before the method executes. */
    TclOO_PostCallProc postCallProc;
				/* Callback to allow for additional cleanup
				 * after the method executes. */
    GetFrameInfoValueProc gfivProc;
				/* Callback to allow for fine tuning of how
				 * the method reports itself. */
} ProcedureMethod;

#define TCLOO_PROCEDURE_METHOD_VERSION 0

/*
 * Flags for use in a ProcedureMethod.
 *
 * When the USE_DECLARER_NS flag is set, the method will use the namespace of
 * the object or class that declared it (or the clone of it, if it was from
 * such that the implementation of the method came to the particular use)
 * instead of the namespace of the object on which the method was invoked.
 * This flag must be distinct from all others that are associated with
 * methods.
 */

#define USE_DECLARER_NS		0x80

/*
 * Forwarded methods have the following extra information. It is a
 * single-field structure because this allows for future expansion without
 * changing vast amounts of code.
 */

typedef struct ForwardMethod {
    Tcl_Obj *prefixObj;
} ForwardMethod;

/*
 * Helper definitions that declare a "list" array. The two varieties are
 * either optimized for simplicity (in the case that the whole array is
 * typically assigned at once) or efficiency (in the case that the array is
 * expected to be expanded over time). These lists are designed to be iterated
 * over with the help of the FOREACH macro (see later in this file).
 *
 * The "num" field always counts the number of listType_t elements used in the
 * "list" field. When a "size" field exists, it describes how many elements
 * are present in the list; when absent, exactly "num" elements are present.
 */

#define LIST_STATIC(listType_t) \
    struct { int num; listType_t *list; }
#define LIST_DYNAMIC(listType_t) \
    struct { int num, size; listType_t *list; }

/*
 * Now, the definition of what an object actually is.
 */

typedef struct Object {
    Tcl_Namespace *namespacePtr;/* This object's tame namespace. */
    Tcl_Command command;	/* Reference to this object's public
				 * command. */
    Tcl_Command myCommand;	/* Reference to this object's internal
				 * command. */
    struct Class *selfCls;	/* This object's class. */
    Tcl_HashTable methods;	/* Object-local Tcl_Obj (method name) to
				 * Method* mapping. */
    LIST_STATIC(struct Class *) mixins;
				/* Classes mixed into this object. */
    LIST_STATIC(Tcl_Obj *) filters;
				/* List of filter names. */
    struct Class *classPtr;	/* All classes have this non-NULL; it points
				 * to the class structure. Everything else has
				 * this NULL. */
    int flags;
    int creationEpoch;		/* Unique value to make comparisons of objects
				 * easier. */
    int epoch;			/* Per-object epoch, incremented when the way
				 * an object should resolve call chains is
				 * changed. */
    Tcl_HashTable *metadataPtr;	/* Mapping from pointers to metadata type to
				 * the ClientData values that are the values
				 * of each piece of attached metadata. This
				 * field starts out as NULL and is only
				 * allocated if metadata is attached. */
    Tcl_Obj *cachedNameObj;	/* Cache of the name of the object. */
    Tcl_HashTable publicContextCache;	/* Place to keep unused contexts. */
    Tcl_HashTable privateContextCache;	/* Place to keep unused contexts. */
    Tcl_ObjectMapMethodNameProc mapMethodNameProc;
				/* Function to allow remapping of method
				 * names. For itcl-ng. */
} Object;

#define OBJECT_DELETED	1	/* Flag to say that an object has been
				 * destroyed. */
#define ROOT_OBJECT 0x1000	/* Flag to say that this object is the root of
				 * the class hierarchy and should be treated
				 * specially during teardown. */
#define FILTER_HANDLING 0x2000	/* Flag set when the object is processing a
				 * filter; when set, filters are *not*
				 * processed on the object, preventing nasty
				 * recursive filtering problems. */

/*
 * And the definition of a class. Note that every class also has an associated
 * object, through which it is manipulated.
 */

typedef struct Class {
    Object *thisPtr;		/* Reference to the object associated with
				 * this class. */
    int flags;			/* Assorted flags. */
    LIST_STATIC(struct Class *) superclasses;
				/* List of superclasses, used for generation
				 * of method call chains. */
    LIST_DYNAMIC(struct Class *) subclasses;
				/* List of subclasses, used to ensure deletion
				 * of dependent entities happens properly when
				 * the class itself is deleted. */
    LIST_DYNAMIC(Object *) instances;
				/* List of instances, used to ensure deletion
				 * of dependent entities happens properly when
				 * the class itself is deleted. */
    LIST_STATIC(Tcl_Obj *) filters;
				/* List of filter names, used for generation
				 * of method call chains. */
    LIST_STATIC(struct Class *) mixins;
				/* List of mixin classes, used for generation
				 * of method call chains. */
    LIST_DYNAMIC(struct Class *) mixinSubs;
				/* List of classes that this class is mixed
				 * into, used to ensure deletion of dependent
				 * entities happens properly when the class
				 * itself is deleted. */
    LIST_STATIC(struct Class *) classHierarchy;
				/* List of classes that comprise the basic
				 * class hierarchy for this class's
				 * superclasses. If NULL (and this isn't the
				 * root object class) then this needs
				 * recomputing. */
    int classHierarchyEpoch;	/* Differs from the global epoch when it is
				 * time to recompute the class hierarchy. */
    Tcl_HashTable classMethods;	/* Hash table of all methods. Hash maps from
				 * the (Tcl_Obj*) method name to the (Method*)
				 * method record. */
    Method *constructorPtr;	/* Method record of the class constructor (if
				 * any). */
    Method *destructorPtr;	/* Method record of the class destructor (if
				 * any). */
    Tcl_HashTable *metadataPtr;	/* Mapping from pointers to metadata type to
				 * the ClientData values that are the values
				 * of each piece of attached metadata. This
				 * field starts out as NULL and is only
				 * allocated if metadata is attached. */
} Class;

/*
 * The foundation of the object system within an interpreter contains
 * references to the key classes and namespaces, together with a few other
 * useful bits and pieces. Probably ought to eventually go in the Interp
 * structure itself.
 */

typedef struct Foundation {
    Class *objectCls;		/* The root of the object system. */
    Class *classCls;		/* The class of all classes. */
    Tcl_Namespace *ooNs;	/* Master ::oo namespace. */
    Tcl_Namespace *defineNs;	/* Namespace containing special commands for
				 * manipulating objects and classes. The
				 * "oo::define" command acts as a special kind
				 * of ensemble for this namespace. */
    Tcl_Namespace *helpersNs;	/* Namespace containing the commands that are
				 * only valid when executing inside a
				 * procedural method. */
    int epoch;			/* Used to invalidate method chains when the
				 * class structure changes. */
    int nsCount;		/* Counter so we can allocate a unique
				 * namespace to each object. */
    Tcl_Obj *unknownMethodNameObj;
				/* Shared object containing the name of the
				 * unknown method handler method. */
    Tcl_Obj *constructorName;	/* Shared object containing the "name" of a
				 * constructor. */
    Tcl_Obj *destructorName;	/* Shared object containing the "name" of a
				 * destructor. */
} Foundation;

/*
 * A call context structure is built when a method is called. They contain the
 * chain of method implementations that are to be invoked by a particular
 * call, and the process of calling walks the chain, with the [next] command
 * proceeding to the next entry in the chain.
 */

#define CALL_CHAIN_STATIC_SIZE 4

struct MInvoke {
    Method *mPtr;		/* Reference to the method implementation
				 * record. */
    int isFilter;		/* Whether this is a filter invokation. */
    Class *filterDeclarer;	/* What class decided to add the filter; if
				 * NULL, it was added by the object. */
};

typedef struct CallContext {
    Object *oPtr;		/* The object associated with this call. */
    int globalEpoch;		/* Global (class) epoch counter snapshot. */
    int localEpoch;		/* Local (single object) epoch counter
				 * snapshot. */
    int flags;			/* Assorted flags, see below. */
    int index;			/* Index into the call chain of the currently
				 * executing method implementation. */
    int refCount;		/* Reference count. */
    int skip;
    int numCallChain;		/* Size of the call chain. */
    struct MInvoke *callChain;	/* Array of call chain entries. May point to
				 * staticCallChain if the number of entries is
				 * small. */
    struct MInvoke staticCallChain[CALL_CHAIN_STATIC_SIZE];
} CallContext;

/*
 * Bits for the 'flags' field of the call context.
 */

#define PUBLIC_METHOD     0x01	/* This is a public (exported) method. */
#define PRIVATE_METHOD    0x02	/* This is a private (class's direct instances
				 * only) method. */
#define OO_UNKNOWN_METHOD 0x04	/* This is an unknown method. */
#define CONSTRUCTOR	  0x08	/* This is a constructor. */
#define DESTRUCTOR	  0x10	/* This is a destructor. */

/*
 * Assorted flags for call frames. Note that bits 1 and 2 are already taken by
 * Tcl itself.
 */

#define FRAME_IS_METHOD	0x4	/* The frame is a method body, and the frame's
				 * clientData field contains a CallContext
				 * reference. */
#define FRAME_IS_OO_DEFINE 0x8	/* The frame is part of the inside workings of
				 * the [oo::define] command; the clientData
				 * field contains an Object reference. */
#define FRAME_IS_CONSTRUCTOR 0x10
				/* The frame is a constructor, and not just a
				 * normal method. */
#define FRAME_IS_DESTRUCTOR 0x20/* The frame is a destructor, and not just a
				 * normal method. */

/*
 * Structure containing definition information about basic class methods.
 */

typedef struct {
    const char *name;		/* Name of the method in question. */
    int isPublic;		/* Whether the method is public by default. */
    Tcl_MethodCallProc callProc;/* How to call the method. */
} DeclaredClassMethod;

/*
 *----------------------------------------------------------------
 * Commands relating to OO support.
 *----------------------------------------------------------------
 */

MODULE_SCOPE int	TclOOInit(Tcl_Interp *interp);
MODULE_SCOPE int	TclOODefineObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineConstructorObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineDestructorObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineExportObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineFilterObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineForwardObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineMethodObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineMixinObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineSuperclassObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineUnexportObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE int	TclOODefineSelfClassObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const *objv);

/*
 * Private definitions, some of which perhaps ought to be exposed properly or
 * maybe just put in the internal stubs table.
 */

MODULE_SCOPE void	TclOOAddToInstances(Object *oPtr, Class *clsPtr);
MODULE_SCOPE void	TclOOAddToMixinSubs(Class *subPtr, Class *mixinPtr);
MODULE_SCOPE void	TclOOAddToSubclasses(Class *subPtr, Class *superPtr);
MODULE_SCOPE void	TclOODeleteContext(CallContext *contextPtr);
MODULE_SCOPE void	TclOODeleteMethod(Method *method);
MODULE_SCOPE CallContext *TclOOGetCallContext(Foundation *fPtr, Object *oPtr,
			    Tcl_Obj *methodNameObj, int flags,
			    Tcl_HashTable *cachePtr);
MODULE_SCOPE Foundation	*TclOOGetFoundation(Tcl_Interp *interp);
MODULE_SCOPE Tcl_Obj *	TclOOGetFwdFromMethod(Method *mPtr);
MODULE_SCOPE Proc *	TclOOGetProcFromMethod(Method *mPtr);
MODULE_SCOPE int	TclOOGetSortedMethodList(Object *oPtr, int flags,
			    const char ***stringsPtr);
MODULE_SCOPE void	TclOOInitInfo(Tcl_Interp *interp);
MODULE_SCOPE int	TclOOInvokeContext(Tcl_Interp *interp,
			    CallContext *contextPtr, int objc,
			    Tcl_Obj *const *objv);
MODULE_SCOPE void	TclOONewBasicClassMethod(Tcl_Interp *interp,
			    Class *clsPtr, const DeclaredClassMethod *dcm);
MODULE_SCOPE Method *	TclOONewForwardClassMethod(Tcl_Interp *interp,
			    Class *clsPtr, int isPublic, Tcl_Obj *nameObj,
			    Tcl_Obj *prefixObj);
MODULE_SCOPE Method *	TclOONewForwardMethod(Tcl_Interp *interp,
			    Object *oPtr, int isPublic, Tcl_Obj *nameObj,
			    Tcl_Obj *prefixObj);
MODULE_SCOPE Tcl_Obj *	TclOOObjectName(Tcl_Interp *interp, Object *oPtr);
MODULE_SCOPE void	TclOORemoveFromInstances(Object *oPtr, Class *clsPtr);
MODULE_SCOPE void	TclOORemoveFromMixinSubs(Class *subPtr,
			    Class *mixinPtr);
MODULE_SCOPE void	TclOORemoveFromSubclasses(Class *subPtr,
			    Class *superPtr);
MODULE_SCOPE void	TclOOStashContext(Tcl_Obj *objPtr,
			    CallContext *contextPtr);

/*
 * Include all the private API, generated from tclOO.decls.
 */

#include "tclOOIntDecls.h"

/*
 * A convenience macro for iterating through the lists used in the internal
 * memory management of objects. This is a bit gnarly because we want to do
 * the assignment of the picked-out value only when the body test succeeds,
 * but we cannot rely on the assigned value being useful, forcing us to do
 * some nasty stuff with the comma operator. The compiler's optimizer should
 * be able to sort it all out!
 *
 * REQUIRES DECLARATION: int i;
 */

#define FOREACH(var,ary) \
	for(i=0 ; (i<(ary).num?((var=(ary).list[i]),1):0) ; i++)

/*
 * Convenience macros for iterating through hash tables. FOREACH_HASH_DECLS
 * sets up the declarations needed for the main macro, FOREACH_HASH, which
 * does the actual iteration. FOREACH_HASH_VALUE is a restricted version that
 * only iterates over values.
 */

#define FOREACH_HASH_DECLS \
    Tcl_HashEntry *hPtr;Tcl_HashSearch search
#define FOREACH_HASH(key,val,tablePtr) \
    for(hPtr=Tcl_FirstHashEntry((tablePtr),&search); hPtr!=NULL ? \
	    ((key)=(void *)Tcl_GetHashKey((tablePtr),hPtr),\
	    (val)=Tcl_GetHashValue(hPtr),1):0; hPtr=Tcl_NextHashEntry(&search))
#define FOREACH_HASH_VALUE(val,tablePtr) \
    for(hPtr=Tcl_FirstHashEntry((tablePtr),&search); hPtr!=NULL ? \
	    ((val)=Tcl_GetHashValue(hPtr),1):0;hPtr=Tcl_NextHashEntry(&search))

/*
 * Convenience macro for duplicating a list. Needs no external declaration,
 * but all arguments are used multiple times and so must have no side effects.
 */

#define DUPLICATE(target,source,type) \
    do { \
	register unsigned len = sizeof(type) * ((target).num=(source).num);\
	if (len != 0) { \
	    memcpy(((target).list=(type*)ckalloc(len)), (source).list, len); \
	} else { \
	    (target).list = NULL; \
	} \
    } while(0)

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
