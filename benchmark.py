#!/usr/bin/env python
# Benchmark script adapted from Wren (https://github.com/wren-lang/wren/)

from __future__ import print_function

import argparse
import math
import os
import os.path
import re
import subprocess
import sys
from os.path import relpath

BENCHMARK_DIR = os.path.join('tests', 'benchmarks')
BENCHMARK_DIR = relpath(BENCHMARK_DIR).replace("\\", "/")
NUM_TRIALS = 10

BENCHMARKS = []

def BENCHMARK(name, pattern):
	regex = re.compile(pattern + r"elapsed: (\d+\.\d+)", re.MULTILINE)
	BENCHMARKS.append([name, regex, None])

BENCHMARK("sort_custom", "")
BENCHMARK("sort", "")
BENCHMARK("for", r"""499999500000\n""")

BENCHMARK("binary_trees", """stretch tree of depth 13 check: -1
8192 trees of depth 4 check: -8192
2048 trees of depth 6 check: -2048
512 trees of depth 8 check: -512
128 trees of depth 10 check: -128
32 trees of depth 12 check: -32
long lived tree of depth 12 check: -1\n""")

BENCHMARK("fib", r"""317811
317811
317811
317811
317811\n""")
BENCHMARK("lit_call", "")
BENCHMARK("c_call", "")

LANGUAGES = [
	("lit",            ["./dist/lit", "-Oall"],          ".lit"),
	("lit (-Ono-all)", ["./dist/lit", "-Ono-all"],       ".lit"),
	("python",         ["python2.7"],                       ".py"),
	("lua",            ["lua"],                          ".lua"),
	("luajit (-joff)", ["luajit", "-joff"],              ".lua"),
	("luajit",         ["luajit"],                       ".lua")
]

results = {}

if sys.platform == 'win32':
	GREEN = NORMAL = RED = YELLOW = ''
else:
	GREEN = '\033[32m'
	NORMAL = '\033[0m'
	RED = '\033[31m'
	YELLOW = '\033[33m'

def green(text):
	return GREEN + text + NORMAL

def red(text):
	return RED + text + NORMAL

def yellow(text):
	return YELLOW + text + NORMAL

def get_score(time):
	"""
	Converts time into a "score". This is the inverse of the time with an
	arbitrary scale applied to get the number in a nice range. The goal here is
	to have benchmark results where faster = bigger number.
	"""
	return 1000.0 / time


def standard_deviation(times):
	"""
	Calculates the standard deviation of a list of numbers.
	"""
	mean = sum(times) / len(times)

	# Sum the squares of the differences from the mean.
	result = 0
	for time in times:
		result += (time - mean) ** 2

	return math.sqrt(result / len(times))


def run_trial(benchmark, language):
	"""Runs one benchmark one time for one language."""
	executable_args = language[1]

	benchmark_path = os.path.join(BENCHMARK_DIR, benchmark[0], 'benchmark' + language[2])
	benchmark_path = relpath(benchmark_path).replace("\\", "/")

	args = []
	args.extend(executable_args)
	args.append(benchmark_path)

	try:
		out = subprocess.check_output(args, universal_newlines=True)
	except OSError:
		print('Interpreter was not found')
		return None
	match = benchmark[1].match(out)
	if match:
		return float(match.group(1))
	else:
		print("Incorrect output:")
		print(out)
		return None


def run_benchmark_language(benchmark, language, benchmark_result):
	"""
	Runs one benchmark for a number of trials for one language.

	Adds the result to benchmark_result, which is a map of language names to
	results.
	"""

	name = "{0} - {1}".format(benchmark[0], language[0])
	print("{0:30s}".format(name), end=' ')

	if not os.path.exists(os.path.join(
		BENCHMARK_DIR, benchmark[0] + '/benchmark' + language[2])):
		print("No implementation for this language")
		return

	times = []
	for i in range(0, NUM_TRIALS):
		sys.stdout.flush()
		time = run_trial(benchmark, language)
		if not time:
			return
		times.append(time)
		sys.stdout.write(".")

	best = min(times)
	score = get_score(best)

	comparison = ""
	if language[0] == "lit":
		if benchmark[2] != None:
			ratio = 100 * score / benchmark[2]
			comparison =  "{:6.2f}% relative to baseline".format(ratio)
			if ratio > 105:
				comparison = red(comparison)
			if ratio < 95:
				comparison = green(comparison)
		else:
			comparison = "no baseline"
	else:
		# Hack: assumes lit gets run first.
		lit_score = benchmark_result["lit"]["score"]
		ratio = 100.0 * lit_score / score
		comparison =  "{:6.2f}%".format(ratio)
		if ratio > 105:
			comparison = red(comparison)
		if ratio < 95:
			comparison = green(comparison)

	print(" {:4.2f}s {:4.4f} {:s}".format(
		best,
		standard_deviation(times),
		comparison))

	benchmark_result[language[0]] = {
		"desc": name,
		"times": times,
		"score": score
	}

	return score


def run_benchmark(benchmark, languages, graph):
	"""Runs one benchmark for the given languages (or all of them)."""

	benchmark_result = {}
	results[benchmark[0]] = benchmark_result

	num_languages = 0
	for language in LANGUAGES:
		if not languages or language[0] in languages:
			num_languages += 1
			run_benchmark_language(benchmark, language, benchmark_result)

	if num_languages > 1 and graph:
		graph_results(benchmark_result)


def graph_results(benchmark_result):
	print()

	INCREMENT = {
		'-': 'o',
		'o': 'O',
		'O': '0',
		'0': '0'
	}

	# Scale everything by the highest score.
	highest = 0
	for language, result in benchmark_result.items():
		score = get_score(min(result["times"]))
		if score > highest: highest = score

	print("{0:30s}0 {1:66.0f}".format("", highest))
	for language, result in benchmark_result.items():
		line = ["-"] * 68
		for time in result["times"]:
			index = int(get_score(time) / highest * 67)
			line[index] = INCREMENT[line[index]]
		print("{0:30s}{1}".format(result["desc"], "".join(line)))
	print()


def read_baseline():
	baseline_file = os.path.join(BENCHMARK_DIR, "baseline.txt")
	if os.path.exists(baseline_file):
		with open(baseline_file) as f:
			for line in f.readlines():
				name, best = line.split(",")
				for benchmark in BENCHMARKS:
					if benchmark[0] == name:
						if not best.startswith("None"):
							benchmark[2] = float(best)
						else:
							benchmark[2] = 0.0


def generate_baseline():
	print("generating baseline")
	baseline_text = ""
	for benchmark in BENCHMARKS:
		best = run_benchmark_language(benchmark, LANGUAGES[0], {})
		baseline_text += ("{},{}\n".format(benchmark[0], best))

	# Write them to a file.
	baseline_file = os.path.join(BENCHMARK_DIR, "baseline.txt")
	with open(baseline_file, 'w') as out:
		out.write(baseline_text)


def print_html():
	'''Print the results as an HTML chart.'''

	def print_benchmark(benchmark, name):
		print('<h3>{}</h3>'.format(name))
		print('<table class="chart">')

		# Scale everything by the highest time.
		highest = 0
		for language, result in results[benchmark].items():
			time = min(result["times"])
			if time > highest: highest = time

		languages = sorted(results[benchmark].keys(),
		                   key=lambda lang: results[benchmark][lang]["score"], reverse=True)

		for language in languages:
			result = results[benchmark][language]
			time = float(min(result["times"]))
			ratio = int(100 * time / highest)
			css_class = "chart-bar"
			if language == "lit":
				css_class += " lit"
			print('  <tr>')
			print('    <th>{}</th><td><div class="{}" style="width: {}%;">{:4.2f}s&nbsp;</div></td>'.format(
				language, css_class, ratio, time))
			print('  </tr>')
		print('</table>')

	print_benchmark("method_call", "Method Call")
	print_benchmark("delta_blue", "DeltaBlue")
	print_benchmark("binary_trees", "Binary Trees")
	print_benchmark("fib", "Recursive Fibonacci")


def main():
	parser = argparse.ArgumentParser(description="Run the benchmarks")
	parser.add_argument("benchmark", nargs='?',
	                    default="all",
	                    help="The benchmark to run")
	parser.add_argument("--generate-baseline",
	                    action="store_true",
	                    help="Generate a baseline file")
	parser.add_argument("--graph",
	                    action="store_true",
	                    help="Display graph results.")
	parser.add_argument("-l", "--language",
	                    action="append",
	                    help="Which language(s) to run benchmarks for")
	parser.add_argument("--output-html",
	                    action="store_true",
	                    help="Output the results chart as HTML")

	args = parser.parse_args()

	if args.generate_baseline:
		generate_baseline()
		return

	read_baseline()

	# Run the benchmarks.
	for benchmark in BENCHMARKS:
		if benchmark[0] == args.benchmark or args.benchmark == "all":
			run_benchmark(benchmark, args.language, args.graph)

	if args.output_html:
		print_html()


main()
