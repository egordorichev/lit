cd ..

rm -rf ./CMakeFiles
rm -rf ./CMakeCache.txt

emcmake cmake .
emmake make

emcc -o3 dist/liblit.a html/glue/glue.c -I include/ -s ALLOW_MEMORY_GROWTH=1 -s RESERVED_FUNCTION_POINTERS=1 -s NO_FILESYSTEM=1 -s NO_EXIT_RUNTIME=1 --shell-file html/shell.html -o dist/index.html -s WASM=1 -s EXPORTED_FUNCTIONS='["_main"]' -s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]'