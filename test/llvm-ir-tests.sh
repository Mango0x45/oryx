#!/bin/sh

readonly OUT=/tmp/llvm-ir-tests-out
trap "rm -f $OUT" EXIT

for f in ./test/data/*.yx
do
	if ! ./oryx -l "$f" \
	| sed '1,/^$/d' \
	| diff -u --color=always - "${f%%yx}ll" >$OUT
	then
		file="$(basename "$f" .yx)"
		echo "llvm-ir-tests: Test ‘$file’ failed" >&2
		printf '\tFailing diff:\n' >&2
		cat $OUT
		exit 1
	fi
done
