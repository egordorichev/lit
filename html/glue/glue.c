#include <lit/lit.h>
#include <stdio.h>

void glue_eval(char* string) {
	lit_eval(string);
}

int main(int argc, char* argv[]) {
	glue_eval("Hello, World!");
	return 0;
}