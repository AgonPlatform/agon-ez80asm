#!/bin/bash
# Positive test - assembler needs to pass all tests in all subfolders
# return 0 on succesfull tests (all passed)
# return 1 on issue during test (one or more tests didn't pass correctly)
# return 2 on error in test SETUP 
#

test_number=0
tests_successfull=0

cd tests
rm -f *.bin
rm -f *.output
for FILE in *; do
    if [ -f "$FILE" ]; then
        if [ "$FILE" == "${FILE%.*}.s" ]; then
            test_number=$((test_number+1))
            "../$ASMBIN" "$FILE" "$@" -c -b FF >> ${FILE%.*}.asm.output
            if [ $? -ne 0 ]; then
                echo "$FILE ASM ERROR"
            else
                echo -n "$FILE ASM OK"
                if [ -f ${FILE%.*}.expect ]; then
                    echo -n " - binary"
                    diff ${FILE%.*}.bin ${FILE%.*}.expect >/dev/null
                    if [ $? -ne 0 ]; then
                        echo " error"
                    else
                        echo " match"
                        tests_successfull=$((tests_successfull+1))
                    fi
                else
                    echo ""
                    tests_successfull=$((tests_successfull+1))
                fi
            fi 
        fi
    fi
done
rm -f *.bin
cd ..

if [ $test_number -eq $tests_successfull ]; then
    echo "All ($test_number) files assembled succesfully"
    exit 0
else
    exit 1
fi
exit 0
