# Control flow

Lit is a turing-complete language. Its VM is able to execute different parts of the bytecode depending on a condition.
Thankfully, you don't have to know lits bytecode in order to write turing-complete code. Let's look at some examples.

## If statement

The first control statement you are going to meet is our good old friend if statement. It takes a condition and executes its branches depending on the evaluation result of the condition expression.

```js
if (false) print("Something went wrong")

if (10 == 5 + 5) {
    print("I knew I could do math")
}

if (10 == 0) {
    excuseMe()
} else if (3 > 4) {
    throwAError()
} else {
    print("Everything above was evaluated to false")
}
```

### Expression version

There is also a short version of if statement, that can be used in an expression:

```js
var result = a > 32 ? b + 1 : c
```

## While loop

Our next stop is loop statements. While loops are as simple as it ever gets:

```js
var i = 10

while (i > 0) {
    i--
}
```

The loop body will be executed until the condition becomes false.

### Continue

You can control when the loop body execution jumps back to the beginning with the continue statement. When it is executed, the rest of the loop body is skipped and the execution jumps back to the loop start.

```js
var i = 0

while (i < 10) {
    i++
    
    if (i % 2 == 0) {
        continue // This way only odd numbers will be printed out
    }
    
    print(i)
}
```

### Break

Break statement is similar to continue, but instead of continuing the execution from the next loop cycle, it aborts the execution.

```js
var i = 0

while (true) {
    if (i > 10) {
        break
    }
    
    i++
}
```

## For loop

Now lets talk about the more complex loop type - for loop. Lit has two variants of it: the classic c-style loop and foreach-style.

### C-style

Here the loop has 4 parts: variable initialization, condition, increment and body.
Typically, they are used in this manner:

```js
for (var i = 10; i < 20; i += 2) {
    print(i)
}
```

But they can be omitted to create an infinite loop:

```js
for (;;) {
    // Prepare to freeze your PC :D
}
```

Or you can find interesting ways of modifying the formula:

```js
var a

for (a = getValue(); a < 10 && a != 7; a = iterate(a)) {
    print(a)
}
```

### Foreach-style

Writing c-style for loops is a bit tedious, so there is a shorthand for iterating numbers:

```js
for (var i in 10 .. 19) {
    print(i)
}
```

Here we are iterating over a range object, going from 10 to 19. But ranges is not the only iterable structure.
In fact, any object with methods `iterator` and `iteratorValue` can be iterated. For example, an array:

```js
var array = [ 1, 2, 9, 10 ]

for (var value in array) {
    print(value)
}
```

Here is a custom iterable object example:

```js
class Iterable {
    constructor() {
        this.array = [ 69, 420 ]
    }
    
    iterator(i) {
        if (!i) return 0
        if (i == this.array.length) return null
        
        return i + 1
    }
    
    iteratorValue(i) {
        return this.array[i]
    }
}

var iterable = new Iterable()

for (var value in iterable) {
    print(value)
}
```

If the syntax for classes looks overwhelming, don't worry, we will talk about classes a bit later on, in the [Classes](/docs/getting_started/classes) section.