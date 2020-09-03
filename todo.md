# todo

* c-api call() is really expensive
* check upvalues
* get/set field can easily become constant instructions
* upvalues can work as references, perhaps???

* optimizations {
 * dead if branchess
 * tail-call optimization?
 * for (var a in 1 .. 3) -> for (var a = 1; a < 4; i++)
 * ranges like 1 .. 3 can be passed as constants
}

* keep classes as privates and then allow to export them?
* more benchmarks

# feature creep
* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* switch statement?