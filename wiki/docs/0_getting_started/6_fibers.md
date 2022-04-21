# Fibers

Lit doesn't have real threading. But it has a concept, called fibers. In basic terms, fiber is a function, that can pause its
execution, and then be continued from where it has left. For example:

```js
var walker = new Fiber(() => {
	print("Walking...")

	// Pause the execution of the fiber and go back to whoever called it
	Fiber.yield()

	print("And walking...")

	// You can even return values with it
	Fiber.yield("I'm tired")
	return 32
})

walker.run() // Expected: Walking...
print("Fiber returned control") // Expected: Fiber returned control

var result = walker.run() // Expected: And walking...
print(result) // Expected: I'm tired

walker.run()

// You can check, if the fiber ended execution yet
print(walker.done) // Expected: true
```

As you can see, `Fiber.yield()` returns execution to the caller, and it can even return some value.
Fibers are lightweight enough for you to use without any back thought.