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

    Toggle create toggle1 1
    for {set i 0} {$i < 5} {incr i} {
	toggle1 activate
	puts [lindex {false true} [toggle1 value]]
    }
    for {set i 0} {$i < $n} {incr i} {
	[Toggle new 1] destroy
    }

    puts ""

    NthToggle create ntoggle1 1 3
    for {set i 0} {$i < 8} {incr i} {
	ntoggle1 activate
	puts [lindex {false true} [ntoggle1 value]]
    }
    for {set i 0} {$i < $n} {incr i} {
	[NthToggle new 1 3] destroy
    }
}

main {*}$argv
