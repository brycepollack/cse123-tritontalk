#!/bin/bash
make clean
make 

# This function runs the executable with the following args:
# $1 -> test_case_id
# $2 -> path to the config file
# It checks the exact order of stdout and exp_out text files
function run_ordered_test() {
    test_input=./test_suite/test$1/test_input.txt
    stdout=./test_suite/test$1/stdout.txt
    stderr=./test_suite/test$1/stderr.txt
    exp_out=./test_suite/test$1/exp_out.txt
    config_path=./test_suite/$2

	./tritontalk -t $1 -p $config_path < $test_input 2>$stderr  1>$stdout
    # ./tritontalk -t $1 -s 0 -r 1 -p $config_path < $test_input 2>$stderr  1>$stdout


    out_cmp=$(<$stdout)
    exp_out_cmp=$(<$exp_out)

    if [ "$out_cmp" == "$exp_out_cmp" ]; then
        echo "Test $1 OK!"
    else
        echo "FAILED TEST $1"
    fi
}

# This function runs the executable with the following args:
# $1 -> test_case_id
# $2 -> path to the config file
# It does not check the exact order of stdout and exp_out text files
function run_unordered_test() {
    test_input=./test_suite/test$1/test_input.txt
    stdout=./test_suite/test$1/stdout.txt
    stderr=./test_suite/test$1/stderr.txt
    exp_out=./test_suite/test$1/exp_out.txt
    config_path=./test_suite/$2

	./tritontalk -t $1 -p < $test_input $config_path 2>$stderr  1>$stdout
    
    sorted_stdout=$(sort $stdout)
    sorted_exp_out=$(sort $exp_out)

    if [ "$sorted_stdout" == "$sorted_exp_out" ]; then
        echo "Test $1 OK!"
    else
        echo "FAILED TEST $1"
    fi
}

#Test 1
run_ordered_test 1 cc_basic.cfg

#Test 2
#Advanced Testing: Checking corruption
run_ordered_test 2 cc_basic.cfg

#Test 3
run_unordered_test 3 sw4.cfg

#Test 4
run_unordered_test 4 sw4.cfg

#Test 5
run_ordered_test 5 cc_basic.cfg

#Test 6
run_ordered_test 6 basic.cfg



# Just in case you want to get even more fancy with your testing :)
# str="a"
# freq=10
# printf "%0.s${str}" $(seq 1 $freq) >> ./test_suite/test_input.txt
# printf "%s\n" >> ./test_suite/test_input.txt
# printf "%s" "exit" >> ./test_suite/test_input.txt
