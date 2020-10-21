# todo

* fix runtime error lines, they often show up at the prev lines
* correct error messages on the start of the file instead of ''
* expected ) got ) will occur if the file has no more chars left

* easier  object creation: {
 name = name
} could be just { = name } or smth

* this.fun?() or smth i need this
* some sort of main function?
* os module with access to shell?

* segfault in LitInfixParseFn infix_rule = get_rule(parser->previous.type)->infix; (get_rule returns null)
* tail-call optimization?
* more benchmarks
* add tests for c-side features, like saving/loading bytecode

# feature creep
* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* switch statement?