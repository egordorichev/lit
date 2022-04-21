# CLI arguments

Lit CLI has arguments, that you pass to it in order to manipulate its behaviour. You can get a list of them via `--help` argument:

```bash
~ $ lit --help
lit [options] [files]
	-o --output [file]	Instead of running the file the compiled bytecode will be saved.
	-n --native [file]	Instead of running the file the compiled code will be embeded into a native runner.
	-O[name] 	        Enables given optimization. For the list of aviable optimizations run with -Ohelp
	-D[name]		    Defines given symbol.
	-e --eval [string]	Runs the given code string.
	-p --pass [args]	Passes the rest of the arguments to the script.
	-i --interactive	Starts an interactive shell.
	-d --dump		    Dumps all the bytecode chunks from the given file.
	-t --time		    Measures and prints the compilation timings.
	-h --help		    I wonder, what this option does.
```

If no code to run is provided, lit will try to run either `main.lbc` or `main.lit` and, if it fails, defaults to an interactive shell.

## `-o --output`

This argument prevents CLI from executing provided code, instead it will be only compiled and saved to the given path.
You can always run the saved bytecode later on:

```bash
lit main.lit -o main.lbc
lit main.lbc
```

`.lbc` extension stands for lit bytecode. You can read more about it in the [Bytecode](/docs/bytecode) section.

## `-n --native`

This argument is similar to `--output`, but instead of saving the raw bytecode it downloads lit sourcecode, compiles it with the bytecode embedded into a special runner.
_Note: this option is only supported on linux for now._

## `-O -Oall -Ono-all`

This argument controls optimization levels and separate optimizations. You can always get a hint with `lit -Ohelp`. Here are all the supported optimization parameters:

### Separate optimizations

You can tweak how lits optimizer behaves in great detail, here are the options that you can play around with:

<table>
<tr><td>constant-folding</td><td>Replaces constants in code with their values.</td></tr>
<tr><td>literal-folding</td><td>Precalculates literal expressions (3 + 4 is replaced with 7).</td></tr>
<tr><td>unused-var</td><td>Removes user-declared all variables, that were not used.</td></tr>
<tr><td>unreachable-code</td><td>Removes code that will never be reached.</td></tr>
<tr><td>empty-body</td><td>Removes loops with empty bodies.</td></tr>
<tr><td>line-info</td><td>Removes line information from chunks to save on space.</td></tr>
<tr><td>private-names</td><td>Removes names of the private locals from modules (they are indexed by id at runtime).</td></tr>
<tr><td>c-for</td><td>Replaces for-in loops with c-style for loops where it can.</td></tr>
</table>

To enable an optimization, for example `line-info`, prefix it with just the `-O` part:

```bash
lit main.lit -Oline-info
```

And to disable an optimization you need to prefix it with `-Ono-`:

```bash
lit main.lit -Ono-line-info
```

### Optimization levels

But before you start trying to tweak every single optimization option by hand, consider having a look at built-in optimization levels.

<table>
<tr><td>Level 0</td><td>No optimizations (same as -Ono-all)</td></tr>
<tr><td>Level 1</td><td>Super light optimizations, sepcific to interactive shell.</td></tr>
<tr><td>Level 2</td><td>(default) Recommended optimization level for the development.</td></tr>
<tr><td>Level 3</td><td>Medium optimization, recommended for the release.</td></tr>
<tr><td>Level 4</td><td>(default for bytecode) Extreme optimization, throws out most of the variable/function names, used for bytecode compilation.</td></tr>
</table>

To activate a level, prefix it with `-O`:

```bash
lit main.lit -O3
```

## `-D`

This argument defines a symbol as if you've inserted `#define` into your code. Let's look at this example:

```js
#ifdef DEBUG
print('Running in debug mode!')
#else
print('Running in release mode!')
#endif
```

Now if we run it without any additional parameters, we will see the release mode message. But if we run it with the `-DDEBUG`:

```bash
~ $ lit -DDEBUG main.lit
Running in debug mode!
```

The code acts with the symbol definition in mind.

## `-e --eval`

This is probably one of the most useful arguments out there, and it is very simple - it just runs the code you pass with it!

```bash
~ $ lit -e "print(32 * 2)"
64
```

## `-p --pass`

This argument stops the parsing of arguments, and everything that is left will be passed to the script in the `args` array:

```bash
~ $ lit -e "print(args)" --pass hello -e 32
[ "hello", "-e", 32 ]
```

## `-i --interactive`

Launches an interactive shell, that allows you to input and execute code.

## `-d --dump`

Instead of executing the compiled code lit will dump its contents and stop:

```bash
~ $ lit -d main.lit 
^^ main ^^
constants:
   0 "print"
   1 10
   2 1
text:
0000    1 GET_GLOBAL   c0 ("print") 	1
0001    | MOVE          2 	c1 (10) 	0
0002    | MOVE          3 	c2 (1) 	    0
0003    | RANGE         2 	3 	2
0004    | CALL          1 	2 	1
0005    | LOAD_NULL     1 	0 	0
0006    | RETURN        1 	1 	0
hex:
0000401F 00404080 004080C0 0100C086 00808064 00000041 00004047 
vv main vv
^^ main ^^
constants:
   0 "print"
   1 10
   2 1
text:
        print(1 .. 10)
0000    1 GET_GLOBAL   c0 ("print") 	1
0001    | MOVE          2 	c1 (10) 	0
0002    | MOVE          3 	c2 (1)  	0
0003    | RANGE         2 	3 	2
0004    | CALL          1 	2 	1
0005    | LOAD_NULL     1 	0 	0
0006    | RETURN        1 	1 	0
hex:
0000401F 00404080 004080C0 0100C086 00808064 00000041 00004047 
vv main vv
```

To better understand what this all means you can refer to the [Bytecode](/docs/bytecode) section.

## `-t --time`

Lit will measure and display the time it takes it to read, preprocess, parse, optimize and emit the code:

```bash
~ $ lit main.lit -t
Reading source: 0.059ms
-----------------------
Preprocessing:  0.001ms
Parsing:        0.014ms
Optimization:   0.002ms
Emitting:       0.076ms

Total:          0.167ms
-----------------------
```

## `-h --help`

Displays a tiny summary of everything said above.