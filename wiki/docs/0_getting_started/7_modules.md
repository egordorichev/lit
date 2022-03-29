# Modules

When your code grows bigger and bigger, it is a common practice to split it into multiple files - modules. Each `.lit` file is a module,
with the name being relative to the starting script file path without the `.lit` extension. For example:

_libs/library.lit_
```js
print(Module.name)
```

_main.lit_
```js
require("libs.library") // Prints out libs.library
```

Modules are executed once upon loading, and the loaded modules are stored in `Module.loaded`:

```js
print(Module.loaded) // { libs.library: Module libs.library, main: Module main }
```

If you require a module for the second time, just it's return value will be returned and no code will be executed.
As [discussed previously](/docs/getting_started/variables#privates), [Module](/docs/modules/core_module/module) class can be used to access top-level variables (privates) of a file. 