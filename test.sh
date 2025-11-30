#!/bin/bash
export ASMBIN=../../bin/ez80asm
NOCOLOR='\033[0m'
RED='\033[0;31m'
GREEN='\033[0;32m'
FORMAT='%-25.25s'

testresult=2
failed=0

if [ -f "bin/ez80asm" ]; then
    # set all shell scripts executable
    find tests -type f -iname "*.sh" -exec chmod +x {} \;

    cd tests
    for FILE in *; do
        if [ -d "$FILE" ]; then
            cd "$FILE"
            printf $FORMAT $FILE
            if [ -f "test.sh" ]; then
                test_number=0
                ./test.sh $@ > ./test.output
                testresult=$?
                if [ $testresult -eq 1 ]; then
                    echo -e " ${RED}FAIL${NOCOLOR}"
                    failed=$((failed+1))
                else
                    if [ $testresult -eq 2 ]; then
                        echo -e "${RED}Config error${NOCOLOR}"
                        exit 1
                    fi
                    echo -e "${GREEN}OK${NOCOLOR}"
                fi
            else
                echo -e "${RED}Testscript missing${NOCOLOR}"
                echo -e "${RED}Test aborted${NOCOLOR}"
                exit 1
            fi
            cd ..
        fi
    done
    cd ..

    if [ $failed -eq 0 ]; then echo -e "${GREEN}All tests passed${NOCOLOR}"
    else
        echo -e -n "${RED}${failed} test"
        if [ $failed -gt 1 ]; then
            echo -n "s"
        fi
        echo -e " failed${NOCOLOR}"
        exit 1
    fi
else
    echo "Please run make first"
    exit 1
fi
