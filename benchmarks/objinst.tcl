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

    Toggle create toggle1 true
    for {set i 0} {$i < 5} {incr i} {
	puts [[toggle1 activate] value]
    }
    for {set i 0} {$i < $n} {incr i} {
	[Toggle new true] destroy
    }

    puts ""

    NthToggle create ntoggle1 true 3
    for {set i 0} {$i < 8} {incr i} {
	puts [[ntoggle1 activate] value]
    }
    for {set i 0} {$i < $n} {incr i} {
	[NthToggle new true 3] destroy
    }
}

main {*}$argv
