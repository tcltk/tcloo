Object Oriented Programming Package for Tcl (TclOO) Version 0.7

Copyright 2005-2012 Donal K. Fellows

License
=======

See the file "license.terms" for the license under which this software is
made available. This file must have been part of the distribution under
which you received this file.

Building
========

TclOO 0.7 uses the TEA3 build system. These instructions are known to work
on Linux, OSX and Windows (with msys installed).

1) Make sure you have a source distribution of Tcl 8.5 somewhere; you will
   need it to build TclOO.

2) Run the configure shell script in this directory. You may well want to
   use the --with-tcl option to tell the script where to find Tcl's build
   descriptor. Using the --prefix option to specify where to install the
   built version is also often useful.

3) Run 'make'.

4) Run 'make test'. There should be no test failures, but some memory stress
   tests are not run under normal conditions as they require a special build
   of Tcl.

5) Run 'make install'. You might need to get elevated privileges to do this
   (e.g. by using 'sudo') to install in a shared area.

Support
=======

Please file bug reports, feature requests and patches at SourceForge under
the Tcl package. <URL:https://sourceforge.net/projects/tcl> To ensure
attention from the relevant maintainer, please use "TclOO Package" for the
Category field. Remember, it is better to file a bug report twice than not
at all!

Simple Example
==============

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

Significant Changes from 0.6 Release
====================================
* Small changes that improve things substantially:
    * Forwarded methods resolve in the object's namespace.
    * Added [info object namespace] to get an object's namespace.
    * Added Tcl_GetObjectName() for fetching the name of an object.
  TIP #354 <URL:http://tip.tcl.tk/354.html>
* Made "varname" method work with array elements.
* Added [info object methodtype] and [info class methodtype].
* Converted configuration of lists of things in classes and objects to work as
  slots, implemented as instances of [oo::Slot] class. 
  TIP #380 <URL:http://tip.tcl.tk/380.html>
* Added introspection of call chains and [nextto] for "skipping ahead" in the
  call chain (useful in "diamond inheritance" situations).
  TIP #381 <URL:http://tip.tcl.tk/381.html>
* Improved the [oo::copy] mechanism to allow greater user control.
  TIP #397 <URL:http://tip.tcl.tk/397.html>

Compatibility Warnings
======================
Names of classes, methods or variables that begin with a hyphen can now cause
issues with some definitions (i.e., they are reserved to slotted operations).
The fix is to precede the name with a "--" argument in the problem definition.

The syntax and semantics of the method called "<cloned>" are now defined.

Some types in the C API have changed from 0.6 to better match the Tcl style of
doing things. ABI compatibility is maintained.
