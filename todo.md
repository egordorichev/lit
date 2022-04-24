# docs

* ??
* ?.
* refs
* binary operators (like floor divide)
* default arg values
* json
* event system
* http
* array & map .forEach()

# api

* File.remove()
* File.create()
* Directory.remove()
* Directory.create()
* File.writeNumber() -> File.writeInt()
* File.readNumber() -> File.readInt()
* File.writeDouble()
* File.readDouble()
* String.byteLength

# todo

* scanner error doens't stop parser
* then(data => {}) is not a valid syntax
* Operator () overloading
* Date class (look into https://tc39.es/proposal-temporal/docs/)
* regex
* statistics on time spent in each instruction

* easier  object creation: {
 name: name
} could be just { name } or smth

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
