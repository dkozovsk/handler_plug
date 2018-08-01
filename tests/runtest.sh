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
diff ./test.err ./test.exp
result=$?
rm -f ./test.err
exit $result
