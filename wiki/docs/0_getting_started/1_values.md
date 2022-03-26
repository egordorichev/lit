# Values

Values are like atoms, that objects are made of. They are immutable, that means that string `"cookie"` will always stay the same.

## Booleans

Boolean is the most basic value there is. As in any language, it can be only `true` or `false`.
They are represented by the [Bool](/docs/modules/core_module/bool) class.

## Numbers

Numbers are represented by doubles, and look just as you would expect:

```js
10
-128
42.98
0b01010
0xff
```

They are represented by the [Number](/docs/modules/core_module/number) class.

## Strings

Strings is where the stuff starts to get interesting. It's an array of bytes, that uses UTF-8 to represent text.
They are surrounded by double quotes (and double quotes only, we do not support single quotes to avoid the confusion!):

```js
"muffin"
```

The string can also span multiple lines:

```js
"cookies
are the
best"
```

### Escape symbols

A few escape symbols are supported:

```js
"\"" // Doublequote
"\\" // Backslash
"\0" // The NULL byte
"\{" // {
"\a" // Alarm beep
"\b" // Backspace
"\f" // Formfeed
"\n" // Newline
"\r" // Carriedge return
"\t" // Tab
"\v" // Vertical tab
```

### Interpolation

If you prefix your double quotes by a dollar sign, the string starts supporting evaluation of expressions inside of double curvy brackets:

```js
print($"9 = {3 + 3}")
```

Strings are instances of the [String](/docs/modules/core_module/string) class.

## Range

Range combines two numbers. It is inclusive and commonly used for iteration:

```js
for (var i in 0 .. 10) {
    print(i)
}
```

But ranges are also useful for getting a subset of an array:

```js
var array = [ 1, 2, 3, 4 ]
print(array[1 .. 2]) // [ 2, 3 ]
```

Ranges are instances of the [Range](/docs/modules/core_module/range) class.

## Null

Null indicates a non-existing value. If you call a function, and it returns nothing, you will receive null.