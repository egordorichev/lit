local a = {}

for i = 0, 1000000 do
	table.insert(a, math.random(-10000, 10000))
end

local start = os.clock()

table.sort(a, function(a, b)
	return a > b
end)

io.write(string.format("elapsed: %.8f\n", os.clock() - start))