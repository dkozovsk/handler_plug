#!/bin/bash
cd ./$testdir
make
diff ./test.err ./test.exp
result=$?
rm -f ./test.err
cd ..
exit $result
