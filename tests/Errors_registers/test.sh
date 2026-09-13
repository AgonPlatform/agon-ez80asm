#!/bin/bash
# Negative test - assembler needs to fail tests in all subfolders
# return 0 on succesfull test (all failed)
# return 1 on issue during test (one or more tests didn't fail correctly)
# return 2 on error in test SETUP 
#

test_number=0
negtest_failed_successfull=0

cd tests
rm -f *.bin
rm -f *.output
for FILE in *; do
    if [ -f "$FILE" ]; then
        if [ "$FILE" == "${FILE%.*}.s" ]; then
            test_number=$((test_number+1))
            "../$ASMBIN" "$FILE" "$@" -c -b FF >> ${FILE%.*}.asm.output
            status=$?
            if [ "$status" -ne 1 ]; then
                echo "$FILE: expected assembler error (exit 1), got $status"
            elif [ -f "${FILE%.*}.bin" ]; then
                echo "$FILE: failed assembly left an output binary"
            elif [ -f "${FILE%.*}.error" ] &&
                 ! grep -F -i -f "${FILE%.*}.error" "${FILE%.*}.asm.output" >/dev/null; then
                echo "$FILE: expected diagnostic not found"
            else
                negtest_failed_successfull=$((negtest_failed_successfull+1))
            fi
        fi
    fi
done
rm -f *.bin
cd ..

if [ $test_number -eq $negtest_failed_successfull ]; then
    echo "Detected all ($test_number) errors succesfully"
    exit 0
else
    exit 1
fi
exit 0
