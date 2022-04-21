# Getting started

If you want to try out lit, you have a few options.

* [**In your browser**](https://egordorichev.github.io/lit-html/).
* **On your computer**. Download and compile lit CLI to run code files.
* **Embedded into your code**. You can also run lit scripts directly from your C/C++ code.

## On your computer

Lit has no _required_ external dependencies (besides Emscripten, but that's only for the HTML5 builds), so you will only need gcc, make & cmake in order to build the CLI.

_On linux, you will also need to install libreadline:_

```bash
sudo apt-get install libreadline6-dev
```

```bash
git clone https://github.com/egordorichev/lit/
cd lit 
cmake .
make
sudo make install
```

That will install lit, and now you are able to access it from the console. Let's do what every programmer ever has to do at least once, and write a hello world program:

```js
print("Hello, world!")
```

Run it, and you should see the familiar message in your terminal:

```bash
~ $ lit hello.lit
Hello, world!
```

Lit CLI has a bunch of useful arguments, like `-e` or `-d`, you can read more about them in the [CLI arguments](/docs/cli_arguments) section.