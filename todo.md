# todo

* c-api call() is really expensive
* check upvalues
* get/set field can easily become constant instructions
* upvalues can work as references, perhaps???

* optimizations {
 * dead if branchess
 * tail-call optimization?
}

* keep classes as privates and then allow to export them?
* more benchmarks

# feature creep
* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* switch statement?