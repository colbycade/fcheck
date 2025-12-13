#!/bin/bash

# Compile the program
gcc submit/fcheck.c -o fcheck -Wall -Werror -O -std=gnu11
if [ $? -ne 0 ]; then
    echo "Compilation failed"
    exit 1
fi

# 1. Map Rules to Expected Error Messages
declare -A rule_messages
rule_messages=(
    ["1"]="ERROR: bad inode."
    ["2a"]="ERROR: bad direct address in inode."
    ["2b"]="ERROR: bad indirect address in inode."
    ["3"]="ERROR: root directory does not exist."
    ["4"]="ERROR: directory not properly formatted."
    ["5"]="ERROR: address used by inode but marked free in bitmap."
    ["6"]="ERROR: bitmap marks block in use but it is not in use."
    ["7"]="ERROR: direct address used more than once."
    ["8"]="ERROR: indirect address used more than once."
    ["9"]="ERROR: inode marked use but not found in a directory."
    ["10"]="ERROR: inode referred to in directory but marked free."
    ["11"]="ERROR: bad reference count for file."
    ["12"]="ERROR: directory appears more than once in file system."
    ["GOOD"]="" # Special ID for good cases
)

# 2. Map Test Cases to Rule IDs
declare -A test_rules
test_rules=(
    # Rule 1
    ["badinode"]="1"
    
    # Rule 2 (Split into Direct vs Indirect messages)
    ["badaddr"]="2a"
    ["badindir1"]="2b"
    ["badindir2"]="2b"
    
    # Rule 3
    ["badroot"]="3"
    ["badroot2"]="3"
    
    # Rule 4
    ["badfmt"]="4"
    ["mismatch"]="4" # Usually fails format check first
    
    # Rule 5
    ["indirfree"]="5"
    ["mrkfree"]="5"
    
    # Rule 6
    ["mrkused"]="6"
    
    # Rule 7
    ["addronce"]="7"
    
    # Rule 8
    ["addronce2"]="8"
    
    # Rule 9
    ["imrkused"]="9"
    
    # Rule 10
    ["imrkfree"]="10"
    
    # Rule 11
    ["badrefcnt"]="11"
    ["badrefcnt2"]="11"
    
    # Rule 12
    ["badlarge"]="12"
    ["dironce"]="12"
    
    # Good Cases
    ["good"]="GOOD"
    ["goodlarge"]="GOOD"
    ["goodlink"]="GOOD"
    ["goodrefcnt"]="GOOD"
    ["goodrm"]="GOOD"
)

# 3. Run Tests
for test_name in "${!test_rules[@]}"; do
    test_file="testcases/$test_name"
    rule_id="${test_rules[$test_name]}"
    expected="${rule_messages[$rule_id]}"
    
    # Handle display name for split rules (e.g., display "2" instead of "2a")
    display_rule=${rule_id//[a-z]/} 

    if [ ! -f "$test_file" ]; then
        echo "WARNING: Test file $test_file not found. Skipping."
        continue
    fi

    # Run fcheck
    output=$(./fcheck "$test_file" 2>&1)
    exit_code=$?

    # Logic for GOOD cases (Expect 0 exit, empty output)
    if [ "$rule_id" == "GOOD" ]; then
        if [ $exit_code -eq 0 ] && [ -z "$output" ]; then
            echo -e "PASS: $test_name"
        else
            echo -e "FAIL: $test_name"
            echo "   Exit Code: $exit_code (Should be 0)"
            echo "   Output:    '$output'"
            echo "   Expected:  [No Output]"
        fi
        continue
    fi

    # Logic for ERROR cases (Expect 1 exit, specific message)
    if [ $exit_code -eq 1 ]; then
        if [ "$output" == "$expected" ]; then
             echo -e "PASS: $test_name"
        else
             echo -e "FAIL: $test_name"
             echo "   Rule:      #$display_rule"
             echo "   Exit Code: $exit_code"
             echo "   Output:    '$output'"
             echo "   Expected:  '$expected'"
        fi
    else
        echo -e "FAIL: $test_name"
        echo "   Rule:      #$display_rule"
        echo "   Exit Code: $exit_code (Should be 1)"
        echo "   Output:    '$output'"
        echo "   Expected:  '$expected'"
    fi
done

echo "--------------------------------"
echo "Testing complete."