package require TclOO

oo::class create Toggle {
    constructor initState {
	variable state $initState
    }
    method value {} {
	variable state
	return $state
    }
    method activate {} {
	variable state
	set state [expr {!$state}]
	return [self]
    }
}
oo::class create NthToggle {
    superclass Toggle
    constructor {initState maxCounter} {
	next $initState
	variable countMax $maxCounter counter 0
    }
    method activate {} {
	variable counter
	variable countMax
	if {[incr counter] >= $countMax} {
	    next
	    set counter 0
	}
	return [self]
    }
}

proc main {n args} {
    incr n 0 ;# sanity check

    set val 1
    Toggle create toggle $val
    for {set i 0} {$i < $n} {incr i} {
	set val [[toggle activate] value]
    }
    puts [lindex {false true} $val]

    set val 1
    NthToggle create ntoggle 1 3
    for {set i 0} {$i < $n} {incr i} {
	set val [[ntoggle activate] value]
    }
    puts [lindex {false true} $val]
}

main {*}$argv
