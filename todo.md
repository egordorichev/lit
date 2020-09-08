# todo

* figure out what to do with object/map being so close, can we go with just one?
* if no, syntax sugar for creating new objects (set fields straight away like in cs new Object { la = 32, b = true etc })

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
