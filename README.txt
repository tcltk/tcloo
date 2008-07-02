Object Oriented Programming Package for Tcl (TclOO) Version 0.5.1

Copyright 2005-2008 Donal K. Fellows

License
=======

See the file "license.terms" for the license under which this software is
made available. This file must have been part of the distribution under
which you received this file.

Building
========

TclOO 0.5 uses the TEA3 build system. These instructions are known to work
on both Linux and Windows (with msys installed).

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
    constructor {} {
        variable v 0
    }
    method add x {
        variable v
        incr v $x
    }
    method value {} {
        variable v
        return $v
    }
    destructor {
        variable v
        puts "Ended with value $v"
    }
}
set sum [summation new]
puts "Start with [$sum value]"
for {set i 1} {$i <= 10} {incr i} {
    puts "Add $i to get [$sum add $i]"
}
summation destroy
