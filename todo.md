# todo

static inline double valueToNum(Value value) {
  DoubleUnion data;
  data.bits = value;
  return data.num;
  double num;
  memcpy(&num, &value, sizeof(Value));
  return num;
}
//< value-to-num
//> num-to-value

static inline Value numToValue(double num) {
  DoubleUnion data;
  data.num = num;
  return data.bits;
  Value value;
  memcpy(&value, &num, sizeof(double));
  return value;
}

* command line args, command line input, color printing
* make var statements into expressions to allow stuff like if (var a = test()) { print(a) }
* fixmeee!! update the emit_pop_continue on regular for loops and in whiles'

 + file std library
 + allow to require modules using local path from the module path (module path + require_path)

* | & ^ >> <<
* creating fibers, yielding and trying
* resizeable stack

# feature creep

* switch statement?
