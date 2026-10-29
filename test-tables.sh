#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
clang -std=gnu11 -Wall -Wextra -O1 -fsanitize=address,undefined tests/parser_test.c -o build/parser_test
for TEST in normal garbage_prefix; do
 ./build/parser_test "tests/$TEST.ts" > "build/test-$TEST.txt"
 grep -q '^ready=2 ' "build/test-$TEST.txt"
done
grep -q '1|5|1|TV Á' build/test-normal.txt
for TEST in bad_PAT_crc bad_PMT_crc; do
 ./build/parser_test "tests/$TEST.ts" > "build/test-$TEST.txt"
 grep -q '^ready=0 ' "build/test-$TEST.txt"
done
./build/parser_test tests/bad_VCT_crc_fallback.ts > build/test-bad_VCT.txt
grep -q '^ready=2 ' build/test-bad_VCT.txt
echo "Pruebas sinteticas de tablas: OK. No se accedio al USB."
