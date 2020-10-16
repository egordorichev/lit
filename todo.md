# todo

* Interesting quirk, that needs to be fixed: using this in lambdas will break, because it won't become an upvalue and it will point to the function itself. Discovered the hard way today

* easier  object creation: {
 name = name
} could be just { = name } or smth

* optimization: remove unused classes/functions/variables

* garbage collector info {
    * trigger
    * get current memory usage
}

* add time for reading file too
* this.fun?() or smth i need this
* require dir.* ?
* some sort of main function?
* os module with access to shell?

* correct error messages on the start of the file instead of ''
* expected ) got ) will occur if the file has no more chars left

* segfault in LitInfixParseFn infix_rule = get_rule(parser->previous.type)->infix; (get_rule returns null)
* tail-call optimization?
* more benchmarks
* add tests for c-side features, like saving/loading bytecode

# feature creep
* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* switch statement?
