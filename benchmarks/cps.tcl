package require TclOO
puts "cps benchmark using TclOO [package provide TclOO]"
# See http://wiki.tcl.tk/18152 for table of comparison

proc cps {script {iters 100000}} {
    set s [uplevel 1 [list time $script $iters]]
    set cps [expr {1/([lindex $s 0]/1e6)}]
    puts "$cps calls per second of: $script"
}

if {$argc > 0} {
    set iterations [lindex $argv 0]
    if {![string is integer -strict $iterations]} {
	puts stderr "\"$iterations\" is not an integer!"
	exit 1
    }
} else {
    set iterations 1000000
}

# ----------------------------------------------------------------------
namespace path oo
class create foo {
    constructor {} {
	variable x 1
    }
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
	return [next],[incr y]
    }
}

# ----------------------------------------------------------------------
puts "Method invokation microbenchmark"
foo create f
f bar
cps {f bar} $iterations
f destroy

puts "Object creation/deletion microbenchmarks"
cps {[foo new] destroy} $iterations
cps {[foo create f] destroy} $iterations
cps {[foo create ::f] destroy} $iterations

puts "Method inherited invokation microbenchmark"
boo create f
f bar
cps {f bar} $iterations
f destroy

puts "Object inherited creation/deletion microbenchmark"
cps {[boo new] destroy} $iterations

foo destroy
