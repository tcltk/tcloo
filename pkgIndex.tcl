if {![package vsatisfies [package provide Tcl] 8.5]} {return}
package ifneeded TclOO 0.1 [list load [file join $dir tcloo.dll] Tcloo]