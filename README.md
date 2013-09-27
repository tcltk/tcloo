Release of TclOO Version 1.0.1
==============================

This officially corresponds to the version of TclOO that is included with Tcl
8.6.1, except for features (notably coroutine support) that require the 8.6
runtime and not-officially-observable differences like the degree of bytecode
compilation support.

TclOO: An Object System for Tcl
===============================

TclOO is an object system for Tcl that has been designed to provide high
performance while still allowing as much flexibility as possible, and to be a
core for other object systems. It supports a single-rooted class-based object
system where classes are themselves subclassable objects, with multiple
inheritance, mixins, procedure-like and forwarded methods, filter methods,
dynamic reconfiguration, etc.

It does not come with a large class library, and it does not force its use
upon user scripts.

The heritage of TclOO can be traced back to a number of other object systems,
notably including XOTcl, incr Tcl, and Snit. It also draws on experience with
object systems in other languages like C++, Java and Ruby (despite being
somewhat different from each of them).

Changes in TclOO 1.0.1
----------------------
Bugfixes for:

* <http://core.tcl.tk/tcl/info/9d61624b3d>

* <http://core.tcl.tk/tcl/info/3603695>

No API changes.

For a full description of all changes, see:

* <http://core.tcl.tk/tcloo/timeline?from=release-1.0&to=release-1.0.1>

Building
--------

TclOO 1.0.1 uses the TEA3 build system. These instructions are known to work
on Linux, OSX and Windows (with msys installed).

1. Make sure you have a source distribution of Tcl 8.5 somewhere; you will
   need it to build TclOO. (Note that this functionality is incorporated
   directly into Tcl 8.6; you do not need this package with that version.)

2. Run the configure shell script in this directory. You may well want to
   use the `--with-tcl` option to tell the script where to find Tcl's build
   descriptor. Using the `--prefix` option to specify where to install the
   built version is also often useful.

3. Run '`make`'.

4. Run '`make test`'. There should be no test failures, but some memory stress
   tests are not run under normal conditions as they require a special build
   of Tcl.

5. Run '`make install`'. You might need to get elevated privileges to do this
   (e.g. by using '`sudo`') to install in a shared area.

Support
-------

Please file bug reports, feature requests and patches on core.tcl.tk under the
Tcl package. <http://core.tcl.tk/tcl/tktnew> To ensure attention from the
relevant maintainer, please use "35. TclOO Package" for the Category field.
Remember, it is better to file a bug report twice than not at all!

Basic Usage of TclOO
====================

Adding up values with TclOO:

    oo::class create summation {
        variable v
        constructor {} {
            set v 0
        }
        method add x {
            incr v $x
        }
        method value {} {
            return $v
        }
        destructor {
            puts "Ended with value $v"
        }
    }
    set sum [summation new]
    puts "Start with [$sum value]"
    for {set i 1} {$i <= 10} {incr i} {
        puts "Add $i to get [$sum add $i]"
    }
    summation destroy

Toasting bread with events and TclOO:

    oo::class create Toaster {
        variable toasting time
        constructor {toastingTime} {
            set time $toastingTime
            set toasting ""
        }
        method toast {breadProduct} {
            if {$toasting ne ""} {
                error "already toasting something"
            }
            set toasting [after $time [namespace code [list \
                    my Toasted $breadProduct]]]
            puts "toasting $breadProduct for you"
        }
        method Toasted {breadProduct} {
            puts "toasted the $breadProduct"
            set toasting ""
        }
        destructor {
            after cancel $toasting
        }
    }
    
    Toaster create quickToaster 30000 ;  # 30 seconds only
    quickToaster toast crumpet
    
    after 40000 {set done ok}
    vwait done ;                         # Run the event loop
    
    quickToaster destroy ;               # Delete the object

Compatibility Warnings
======================
Names of classes, methods or variables that begin with a hyphen can now cause
issues with some definitions (i.e., they are reserved to slotted operations).
The fix is to precede the name with a "`--`" argument in the problem definition;
see the `oo::define` documentation for the affected definitions.

Method names that are proper multi-element lists are reserved for future
functionality.
