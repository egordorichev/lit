# todo

* remove ./ from the file paths (so that we dont end up with ..main)
* c-api call() is really expensive
* check upvalues
* upvalues can work as references, perhaps???
* tail-call optimization?
* keep classes as privates and then allow to export them?
* more benchmarks

* add tests for c-side features, like saving/loading bytecode

# feature creep
* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* switch statement?