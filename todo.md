# todo

* keep classes as privates and then allow to export them?
* command line option to dump the chunks
* we don't really need private names in bytecode (do an optimization flag that is on by default)
* optimization levels (special one for repl, where unused vars stick around)

* optimizations {
 * dead if branchess
 * multiplication by 1
 * tail-call optimization?
 * for (var a in 1 .. 3) -> for (var a = 1; a < 4; i++)
}

* more benchmarks
* update readme with proper example links (and docs links)
* enums
* html5 version auto-build & demo

* if argument expected type doesnt match in native function, like require, it will crash (cuz it still continues to operate)
* check how syncing works
* table with globals? module.privates?
* resizeable stack
* ffi?

* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* emit_pop_continue needs to work with nested loops

# feature creep

* switch statement?
