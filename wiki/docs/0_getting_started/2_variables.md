# Variables

Variables are basically named registers, that can hold a value. You can define new variable using the var statement:

```js
var sum = 10 + 3
```

This creates a new variable in the current scope and initializes it with the result of executing the expressions on the right of `=`.
Once you define a variable, you can access it by its name, until it leaves the current scope:

```js
var cake = "brownie"
print(cake) // brownie

cake = "apple pie"
print(cake) // apple pie
```

### Globals

If you try to write to a variable, that is not directly declared in the current scope, lit will modify global variables.

### Privates

Top-level module variables are called privates and are accessible by their names via their names:

```js
var a = 32 + 2
var b = "test"

// Note, that d is already in that map, even tho it was not initializated yet!
print(Module.privates) // { _module = Module tests.examples.modules, d = null, a = 34, b = test }

var d = 48
```