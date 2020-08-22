cd ..
emcc -o3 dist/liblit.bc html/glue/glue.c -I include/ -s ALLOW_MEMORY_GROWTH=1 -s RESERVED_FUNCTION_POINTERS=1 -s NO_FILESYSTEM=1 -s NO_EXIT_RUNTIME=1 --shell-file html/shell.html -o dist/index.html -s WASM=1 -s EXPORTED_FUNCTIONS='["_main", "_interpret", "_create_state", "_free_state"]' -s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]'
cd html
