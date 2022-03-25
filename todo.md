# register based

* implement else-if's
* implement non c-style for
* ref's
* multiarg
* & ^ >> << | #
* lambdas
* thing?.call()
* interpolation
* invoking static methods

# todo

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