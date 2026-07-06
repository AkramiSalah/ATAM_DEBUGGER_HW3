#!/bin/bash

cd ./tests
gcc -std=c99 *.c -o prf
if [ ! -f "prf" ]; then
    echo "Compilation error - cannot create debugger file"
    exit 1
fi

# Helper function to run tests and print informative errors
run_test() {
    local test_num=$1
    local expected_exit=$2
    local expected_out_file=$3
    shift 3 # Remove the first 3 args, the rest are the command to run

    # Run the command and capture output
    timeout 20s "$@" > studentout.txt
    local actual_exit=$?

    # Check if exit code matches AND if the diff is empty
    if [ $actual_exit -eq $expected_exit ] && diff -q "$expected_out_file" studentout.txt >/dev/null 2>&1; then
        echo -e "\e[32mTest $test_num: PASS\e[0m" # Prints PASS in green
    else
        echo -e "\e[31mTest $test_num: FAIL\e[0m" # Prints FAIL in red
        
        if [ $actual_exit -ne $expected_exit ]; then
            echo "  -> Exit Code Mismatch: Expected $expected_exit, but got $actual_exit"
            if [ $actual_exit -eq 124 ]; then
                echo "  -> (Exit code 124 means the 20s timeout was reached - possible infinite loop)"
            fi
        fi

        echo "  -> Diff Output (- Expected, + Actual):"
        diff -u "$expected_out_file" studentout.txt
        echo "--------------------------------------------------"
    fi
}

# --- Run the tests ---
# Usage: run_test <TestNumber> <ExpectedExitCode> <ExpectedOutputFile> <CommandToRun...>

run_test 1  0 "out1.txt"  ./prf foo 2 program1.out
run_test 2  0 "out2.txt"  ./prf rec_foo 1 program2.out
run_test 3  0 "out3.txt"  ./prf rec_bar 1 program3.out
run_test 4  0 "out4.txt"  ./prf food 1 program4.out
run_test 5  0 "out5.txt"  ./prf food 1 program5.out
run_test 6  0 "out6.txt"  ./prf food 1 program6.out
run_test 7  0 "out7.txt"  ./prf foo 2 program7.out
run_test 8  0 "out8.txt"  ./prf foo 2 program8.out
run_test 9  0 "out9.txt"  ./prf foo 2 program9.out
run_test 10 0 "out10.txt" ./prf foo 1 program10.out
run_test 11 0 "out11.txt" ./prf foo 2 program11.out R

# Note: Test 12 expects an exit code of 1 (symbol not found)
run_test 12 1 "out12.txt" ./prf nonexistent 1 program1.out 

run_test 13 0 "out13.txt" ./prf zero_params_func 0 program13.out
run_test 14 0 "out14.txt" ./prf six_params_func 6 program14.out
run_test 15 0 "out15.txt" ./prf fib 1 program15.out
run_test 16 0 "out16.txt" ./prf deep_rec_func 1 program16.out
run_test 17 0 "out17.txt" ./prf fib 1 program17.out

# Cleanup
rm -f "prf" "studentout.txt"