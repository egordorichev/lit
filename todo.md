# todo

* if function is declared after the current function and is not exported, it will not find it, cuz it will to look for global
 - we just need a pass on the emitter, that first resolves all the locals

* c-api call() is really expensive
* check upvalues
* get/set field can easily become constant instructions
* upvalues can work as references, perhaps???
* tail-call optimization?
* keep classes as privates and then allow to export them?
* more benchmarks

# feature creep
* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* switch statement?