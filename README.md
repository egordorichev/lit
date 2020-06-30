# lit

Lit scripting language.

```js
class Best : Awesome {
	printTest() {
		print(this.test())
	}

	test() {
		this.prop = "bad"
		return super.test()
	}

	getTest() {
		return super.test
	}

	z => 32

	d {
		get {
			return this.prop
		}

		set => this.prop = value
	}
}
```
