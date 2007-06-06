package require TclOO

oo::class create Toggle {
    constructor initState {
	variable state $initState self [self]
    }
    method value {} {
	variable state
	return $state
    }
    method activate {} {
	variable state
	variable self
	set state [expr {!$state}]
	return $self
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
	variable self
	if {[incr counter] >= $countMax} {
	    next
	    set counter 0
	}
	return $self
    }
}

proc main {n args} {
    set tf {false true}
    incr n 0 ;# sanity check

    set val 1
    Toggle create toggle $val
    for {set i 0} {$i < $n} {incr i} {
	toggle activate
	set val [toggle value]
    }
    puts [lindex $tf $val]

    set val 1
    NthToggle create ntoggle 1 3
    for {set i 0} {$i < $n} {incr i} {
	ntoggle activate
	set val [ntoggle value]
    }
    puts [lindex $tf $val]
}

main {*}$argv
