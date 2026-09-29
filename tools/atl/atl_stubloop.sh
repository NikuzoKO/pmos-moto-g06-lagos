#!/bin/sh
# Compile ATL's api-impl sources with javac and remove ATL-autostub lines that
# fail, until it compiles. Errors on non-stub lines are printed for manual fixes.
S=$HOME/src/atl/src/api-impl
G=$HOME/src/atl/output/src/api-impl
OUT=$(mktemp -d)
LIST=$OUT/files
find $S $G -name '*.java' > $LIST
for pass in 1 2 3 4 5 6 7 8 9 10; do
	rm -rf $OUT/classes; mkdir $OUT/classes
	javac -nowarn -Xmaxerrs 100000 -g:none -encoding UTF-8 -source 1.8 -target 1.8 \
		-bootclasspath /usr/lib/java/core-all_classes.jar:/usr/lib/java/core-junit_classes.jar:/usr/lib/java/junit-runner_classes.jar \
		-d $OUT/classes -sourcepath $S:$G @$LIST > $OUT/log 2>&1
	errs=$(grep -E '^/.*\.java:[0-9]+: error' $OUT/log | cut -d: -f1,2 | sort -u)
	if [ -z "$errs" ]; then
		echo "pass $pass: clean"
		break
	fi
	stub=""
	other=""
	for e in $errs; do
		f=${e%:*}; l=${e##*:}
		if sed -n "${l}p" "$f" | grep -q 'ATL-autostub'; then stub="$stub $e"; else other="$other $e"; fi
	done
	echo "pass $pass: $(echo $stub | wc -w) stub errors, $(echo $other | wc -w) other"
	if [ -z "$stub" ]; then
		for e in $other; do grep -A2 "^$e: error" $OUT/log | head -3; done
		break
	fi
	python3 $HOME/src/atl_autostub.py --prune $stub
done
rm -rf $OUT
