#!/bin/bash
cd ./$testdir
result=$?
if [ $result -ne 0 ] 
then
    echo "cd error"
    exit 1
fi
make
result=$?
if [ $result -ne 0 ] 
then
    echo "make error"
    exit 1
fi
cat ./test.err | tr -d ‘\' >test.tmp

diff ./test.tmp ./test.exp
result=$?
rm -f ./test.err ./test.tmp
exit $result
