# Classes

Everything in lit is an instance of some class. Functions are instances of [Function](/docs/modules/core_module/function) class,
Strings are instances of [String](/docs/modules/core_module/string) class.

Classes describe instance methods.

## Defining a class

Classes are created using the `class` keyword:

```js
class Awesome {
    
}
```

This defines a global variable with class Awesome in it.

## Creating an instance

To create an instance, call the class using the `new` operator. You can also pass arguments to class constructor, like usual.

```js
var awesome = new Awesome()
print(awesome)
```

## Fields

Each object consists of fields. You can write and read from them with ease:

```js
var object = new Object()
print(object.prop) // null, it's not set yet

object.prop = 32
print(object.prop) // 32
```

Sometimes you need to set a lot of fields when creating an object, so there is a handy shortcut for that:

```js
var object = new Object() {
    propA: 32,
    propB: "test"
}

print(object.propB + object.propA)
```

## Methods

Objects field can be a function. You can assign a function to a field by hand, but it's better to do so when defining the class:

```js
class Better {
    greet(who) {
        print($"Hello, {who}!")
    }
}

new Better().greet("Egor")
```

This way all the objects share a single method instance, so it's more memory efficient.
You can also access the objects fields inside of methods using `this` keyword:

```js
class Person {
    constructor(age) {
        this.age = age
    }
    
    greet() {
        print($"Hello, I'm {this.age} years old")
    }
}

new Person(42).greet()
```

`constructor` is a special method name, it defines a method that will be called upon creating an instance of the class, 
and it receives all the arguments that you pass to the `new` expression.

## Static fields

While objects have their own fields, that they do not share, classes have their own "static" fields. They are defined
right in the class statement:

```js
class ImRunningOutOfNames {
    static var name = "Pumba"
}

ImRunningOutOfNames.value = 32

print(ImRunningOutOfNames.name, ImRunningOutOfNames.value)
```

## Super classes

Classes can extend each other, inheriting super class methods:

```js
class Animal {
    constructor(sound) {
        this.sound = sound
    }
    
    makeSound() {
        print(this.sound)
    }
}

class Cat : Animal {
    constructor() {
        super("meow")
    }
    
    makeSound() {
        super.makeSound()
        super.makeSound()
    }
}

var cat = new Cat()
cat.makeSound() // meow, meow
```

Note the use of the `super` keyword to call versions of methods defined in the super class.

## Type checking

Sometimes you need to identify variables type, and what better way to do that then with the `is` operator! 
It takes your variable and any class, and tells you if the variable is an instance of that class.

```js
print(10 is Number) // true
print(new Map() is Object) // true
print(null is String) // false
```

## Operator overloading

And for the desert, let's talk operator overloading. In short, it's a concept that allows you to define how your object behaves
when used with an operator. Let's look at an example:

```js
class Vector {
    constructor(x, y) {
        this.x = x
        this.y = y
    }
    
    print() {
        print($"({this.x}, {this.y})")
    }
    
    operator + (v) {
        return new Vector(this.x + v.x, this.y + v.y)
    }
}

var vector = new Vector(10, 32) + new Vector(1, 3)
vector.print()
```

In this example, the class `Vector` defines its behaviour when used with the plus binary operator.