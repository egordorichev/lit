# Functions

As your program grows, the need for repeating code arises. In order to avoid copy-pasting or to hide some implementation details,
programmers for decades have been using chunks of code with names - functions. They can accept arguments and return a single value.

## Creating a function

Lit has a special function statement for creating functions. It contains the function name, arguments and its body.

```js
function hello() {
    print("Hello, world!")
}

hello()
```

To call a function we use its name together with `()` (any arguments go inside of the parens).

## Function parameters

Functions can define local variables, that receive their values from the outside - arguments:

```js
function greet(who, to) {
    print($"Welcome, {who}, to {to}!")
}

greet("Mr. Maanex", "Moscow")
greet("Mrs. Steph", "London")
```

To pass an argument to the function we put it the parens. All extra arguments are ignored, and if some argument is missing it will be replaced with `null`.

## Returning values

If execution of the function body ends with a return statement, the evaluated expression of the return statement will be returned. Otherwise, `null` is returned.

```js
function getSum(a, b) {
    return a + b
}

print(getSum(10, 9))
```

## Closures

In lit, functions are closures. That means, that they can access variables from a higher above scope and hold onto them, until they are not required anymore.

```js
function createGetter(a) {
    function getter() {
        return a
    }
    
    return getter
}

var getter = createGetter(42)
print(getter())
```

## Varg

A function can opt-in into receiving an unlimited amount of arguments (well, in theory, in reality it is limited to around 255).
All the extra arguments will be stored in an argument with special name `...`. You can treat it like any other argument:

```js
function printAll(...) {
	for (var a in ...) {
		print(a)
	}
}

function shout(a, ...) {
	var b = ...

	print(a)
	printAll(...)
	print(b)
}

shout("freedom", "to", "potatoes")
```