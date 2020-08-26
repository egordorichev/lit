# todo

* optimizations {
 * we don't really need private names in bytecode (do an optimization flag that is on by default), nor function names?
 * dead if branchess
 * multiplication by 1
 * tail-call optimization?
 * for (var a in 1 .. 3) -> for (var a = 1; a < 4; i++)
 * optimize out line numbers
}

* improve repl (history, left/right arrow)
* keep classes as privates and then allow to export them?
* more benchmarks
* enums

* if argument expected type doesnt match in native function, like require, it will crash (cuz it still continues to operate)
* table with globals? module.privates?
* ffi?
* gc prooof testing

# feature creep

* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* switch statement?