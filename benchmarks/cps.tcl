package require TclOO
puts "cps benchmark using TclOO [package provide TclOO]"
# See http://wiki.tcl.tk/18152 for table of comparison

proc cps {script} {
    # Eat the script compilation costs
    uplevel 1 [list time $script]
    # Have a guess at how many iterations to run for around a second
    set s [uplevel 1 [list time $script 5]]
    set iters [expr {round(1/([lindex $s 0]/1e6))}]
    if {$iters < 50} {
	puts "WARNING: number of iterations low"
    }
    # The main timing run
    set s [uplevel 1 [list time $script $iters]]
    set cps [expr {round(1/([lindex $s 0]/1e6))}]
    puts "$cps calls per second of: $script"
}

# ----------------------------------------------------------------------
namespace path oo
class create foo {
    constructor {} {
	variable x 1
    }
    method emptyMethod {} { }
    method bar {} {
	variable x
	set x [expr {!$x}]
    }
}

class create boo {
    superclass foo
    constructor {} {
	next
	variable y 0
    }
    method bar {} {
	variable y
	incr y
	next
    }
}

# ----------------------------------------------------------------------
puts "Method invokation microbenchmark"
foo create f
f bar
cps {f bar}
cps {f emptyMethod}
f destroy

puts "Object creation/deletion microbenchmarks"
cps {[foo new] destroy}
cps {[foo create f] destroy}
cps {[foo create ::f] destroy}

puts "Combined microbenchmark"
cps {foo create ::f;f bar;f destroy}

puts "Method inherited invokation microbenchmark"
boo create f
f bar
cps {f bar}
cps {f emptyMethod}
f destroy

puts "Object inherited creation/deletion microbenchmark"
cps {[boo new] destroy}
cps {[boo create f] destroy}
cps {[boo create ::f] destroy}

puts "Combined inherited microbenchmark"
cps {boo create ::f;f bar;f destroy}

foo destroy
