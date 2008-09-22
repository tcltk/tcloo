set auto_path "[list [pwd]] $auto_path"
package require TclOO 0.6a1
puts "cps benchmark using TclOO [package provide TclOO]"
# See http://wiki.tcl.tk/18152 for table of comparison

# ----------------------------------------------------------------------
# cps --
#	A wrapper round [time] to make it better for performance analysis of
#	very fast code. It works by tuning the number of iterations used until
#	the run-time of the code is around a second.
#
proc cps {script} {
    # Eat the script compilation costs
    uplevel 1 [list time $script]

    # Have a guess at how many iterations to run for around a second
    set s [uplevel 1 [list time $script 5]]
    set iters [expr {round(1.1/([lindex $s 0]/1e6))}]
    if {$iters < 50} {
	puts "WARNING: number of iterations low"
    }

    # The main timing run
    while 1 {
	set s [uplevel 1 [list time $script $iters]]
	# Only use the run if it was for at least a second, otherwise increase
	# the number of iterations and try again.
	if {[lindex $s 0]*$iters >= 1e6} {
	    break
	}
	incr iters $iters
    }

    # Produce the results
    set cps [expr {round(1/([lindex $s 0]/1e6))}]
    puts "$cps calls per second of: [string trim $script]"
}

# ----------------------------------------------------------------------
#namespace path oo
oo::class create base {
    variable x
    constructor {} {
	set x 1
    }
    method emptyMethod {} { }
    method stateful {} {
	set x [expr {!$x}]
    }
    method stateless {} {
	set local 1
	expr {!$local}
    }
}

oo::class create subCls {
    superclass base
    variable y
    constructor {} {
	next
	set y 0
    }
    method stateful {} {
	incr y
	next
    }
    method stateless {} {
	expr {![next]}
    }
}

# This code provides a baseline speed so that we can see how well Tcl itself
# is performing independently of TclOO...
set ::baselinex 1
proc baselineProc {} {
    global baselinex
    set baselinex [expr {!$baselinex}]
}
# ----------------------------------------------------------------------
puts "Baseline..."
cps {baselineProc }
puts "Method invokation microbenchmark"
base create baseObj
cps {baseObj stateless}
cps {baseObj stateful}
cps {baseObj emptyMethod}
base create base2
cps {baseObj stateless;base2 stateless}
base2 destroy
baseObj destroy

puts "Object creation/deletion microbenchmarks"
cps {[base new] destroy}
cps {[base create obj] destroy}
cps {[base create ::obj] destroy}

puts "Combined microbenchmark"
cps {base create ::obj;obj stateless;obj destroy}

puts "Method inherited invokation microbenchmark"
subCls create subObj
cps {subObj stateless}
cps {subObj stateful}
cps {subObj emptyMethod}
subObj destroy

puts "Object inherited creation/deletion microbenchmark"
cps {[subCls new] destroy}
cps {[subCls create obj] destroy}
cps {[subCls create ::obj] destroy}

puts "Combined inherited microbenchmark"
cps {subCls create ::obj;obj stateless;obj destroy}

base destroy
