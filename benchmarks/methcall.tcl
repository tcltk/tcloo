package require TclOO

oo::class create Toggle {
    constructor initState {
	my variable state
	set state $initState
    }
    method value {} {
	my variable state
	return [expr {$state ? "true" : "false"}]
    }
    method activate {} {
	my variable state
	set state [expr {!$state}]
	return [self]
    }
}
oo::class create NthToggle {
    superclass Toggle
    constructor {initState maxCounter} {
	next $initState
	my variable countMax counter
	set countMax $maxCounter
	set counter 0
    }
    method activate {} {
	my variable counter countMax
	incr counter
	if {$counter >= $countMax} {
	    next
	    set counter 0
	}
	return [self]
    }
}

proc main {n args} {
    incr n 0 ;# sanity check

    set val true
    Toggle create toggle $val
    for {set i 0} {$i < $n} {incr i} {
	set val [[toggle activate] value]
    }
    puts $val

    set val true
    NthToggle create ntoggle true 3
    for {set i 0} {$i < $n} {incr i} {
	set val [[ntoggle activate] value]
    }
    puts $val
}

main {*}$argv
