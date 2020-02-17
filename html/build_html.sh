cd ..

rm -rf ./CMakeFiles
rm -rf ./CMakeCache.txt

emcmake cmake .
emmake make

cd html
./light_build.sh