# lit

Lit is a dynamicaly-typed language, inspired by highly-hackable Lua and familiar C-styled JavaScipt.

|  | Lua | JavaScript | Lit | |
|-|-|-|-|-|
| Easy classes | &#x274C; | &#10004; | &#10004; | [Example]() |
| Arrays start from | 1 (WHAT) | 0 | 0 | Balance in the universe is restored |
| String interpolation | &#x274C; | &#10004; | &#10004; | [Example]() |
| Operator overloading | &#10004; | &#x274C; | &#10004; | [Example]() |
| Easy to embed | &#10004; | &#x274C; | &#10004; | [Example]() |
| Has no `undefined` | &#10004; | &#x274C; | &#10004; | Seriously, why, JS?? |
| Syntax sugar like `+=` | &#x274C; | &#10004; | &#10004; | [Example]() |
| Coroutines (fibers) | &#10004; | &#x274C; | &#10004; | [Example]() |

Sounds good? [Check out some challenge solutions](https://github.com/egordorichev/lit/tree/master/test/challenges) or try lit for yourself!

# Building

Lit has no external dependencies (besides Emscripten, but that's only for the HTML5 builds), so you will only need gcc, make & cmake.

```bash
git clone https://github.com/egordorichev/lit/ && cd lit 
cmake .
make
sudo make install
```

That should install lit, and you should be able to access it from the console. Let's write our first program:

```js
print("Hello, world!")
```

Just run it, and you should see the familiar message in your terminal:

```bash
lit hello.lit
```

That's it! Now you are ready to face a more serious challenge, like the [docs]() or [examples](https://github.com/egordorichev/lit/tree/master/test/challenges)!
