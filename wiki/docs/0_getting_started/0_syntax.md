# Syntax

Lit is designed to look familiar to anyone, who touched any C-family language before. It resembles JavaScript a lot.
Programs are stored in files with the `.lit` extensions as plain text and executed from top to bottom.

## Comments

When you want lit to ignore a line, we prefix it with a double slash:

```js
// This is a comment!
var a = 32 // This is a comment too
```

When the comment gets big and spans multiple lines it's better to use multiline comment syntax:

```js
/*
 Look at me
 I'm multiline!
 */
```

## Identifiers

Naming rules are generally simple: identifiers start with a lowercase letter or underscore, and may include digits, characters and underscores.
Lit is case-sensitive.

```js
hello
camelCase
PascalCase
SEND_HELP
```

## Reserved words

Some identifiers in lit are reserved for special syntax constructions, here are all of them:

```js
class, else, false, for, function, if, null, return, super,
this, true, var, while, continue, break, new, export, is,
static, operator, get, set, in, const, ref
```