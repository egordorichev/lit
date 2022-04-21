# Random

## Static methods

### setSeed(seed)

Sets global random instance seed. Seed determines how the random numbers are generated.
If the program starts with the same random seed every time, the same sequence of "random"
numbers will be generated.

### int(min, max)

Returns a random integer in the range between `min` and `max`.

### int(max)

Returns a random integer in the range between `0` and `max`.

### int()

Returns a random integer in the range between `0` and `RAND_MAX`.

### float(min, max)

Returns a random number in the range between `min` and `max`.

### float(max)

Returns a random number in the range between `0` and `max`.

### float()

Returns a random number in the range between `0` and `RAND_MAX`.

### bool()

Returns either `true` or `false` with a 50% chance for either case.

### chance(chance)

Generates a random number between `0` and `100` and returns `true`, if it was smaller than `chance`.

### chance()

Generates a random number between `0` and `100` and returns `true`, if it was smaller than `50`.

### pick(from)

Returns a random element from given array or map with equal weight for every element in the structure.

## Instance methods

### constructor(seed)

Returns a new `Random` instance with random seed set to `seed`.

### constructor()

Returns a new `Random` instance.

### setSeed(seed)

Sets instance seed to `seed`. Seed determines how the random numbers are generated.
If the program starts with the same random seed every time, the same sequence of "random"
numbers will be generated.

### int(min, max)

Returns a random integer in the range between `min` and `max`.

### int(max)

Returns a random integer in the range between `0` and `max`.

### int()

Returns a random integer in the range between `0` and `RAND_MAX`.

### float(min, max)

Returns a random number in the range between `min` and `max`.

### float(max)

Returns a random number in the range between `0` and `max`.

### float()

Returns a random number in the range between `0` and `RAND_MAX`.

### bool()

Returns either `true` or `false` with a 50% chance for either case.

### chance(chance)

Generates a random number between `0` and `100` and returns `true`, if it was smaller than `chance`.

### chance()

Generates a random number between `0` and `100` and returns `true`, if it was smaller than `50`.

### pick(from)

Returns a random element from given array or map with equal weight for every element in the structure.