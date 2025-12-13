#!/bin/bash

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"

# Define paths relative to the script
SRC_FILE="$SCRIPT_DIR/../submit/fcheck.c"
EXEC_FILE="$SCRIPT_DIR/fcheck"

# Compile the program
echo "Compiling..."
gcc "$SRC_FILE" -o "$EXEC_FILE" -Wall -Werror -O -std=gnu11
if [ $? -ne 0 ]; then
    echo "Compilation failed. Checked path: $SRC_FILE"
    exit 1
fi
echo "Compilation successful. Running tests..."
echo "--------------------------------"

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
    ["badinode"]="1"
    ["badaddr"]="2a"
    ["badindir1"]="2b"
    ["badindir2"]="2b"
    ["badroot"]="3"
    ["badroot2"]="3"
    ["badfmt"]="4"
    ["mismatch"]="4"
    ["indirfree"]="5"
    ["mrkfree"]="5"
    ["mrkused"]="6"
    ["addronce"]="7"
    ["addronce2"]="8"
    ["imrkused"]="9"
    ["imrkfree"]="10"
    ["badrefcnt"]="11"
    ["badrefcnt2"]="11"
    ["badlarge"]="12"
    ["dironce"]="12"
    ["good"]="GOOD"
    ["goodlarge"]="GOOD"
    ["goodlink"]="GOOD"
    ["goodrefcnt"]="GOOD"
    ["goodrm"]="GOOD"
)

# 3. Run Tests
for test_name in $(echo "${!test_rules[@]}" | tr ' ' '\n' | sort); do
    test_file="$SCRIPT_DIR/$test_name"
    rule_id="${test_rules[$test_name]}"
    expected="${rule_messages[$rule_id]}"
    display_rule=${rule_id//[a-z]/} 

    if [ ! -f "$test_file" ]; then
        echo "WARNING: Test file $test_file not found. Skipping."
        continue
    fi

    # Run fcheck using the absolute path
    output=$("$EXEC_FILE" "$test_file" 2>&1)
    exit_code=$?
    test_passed=false

    # Logic for GOOD cases
    if [ "$rule_id" == "GOOD" ]; then
        if [ $exit_code -eq 0 ] && [ -z "$output" ]; then
            test_passed=true
        fi
    # Logic for ERROR cases
    else
        if [ $exit_code -eq 1 ] && [ "$output" == "$expected" ]; then
            test_passed=true
        fi
    fi

    # Output Result
    if [ "$test_passed" = true ]; then
        echo -e "PASS: $test_name"
    else
        echo -e "FAIL: $test_name"
        
        # Only print details if it's a BAD image failure (as requested)
        if [ "$rule_id" != "GOOD" ]; then
            echo "   Rule:     #$display_rule"
            echo "   Expected: '$expected'"
            echo "   Actual:   '$output'"
        fi
        # Good images failing print nothing extra, just "FAIL: test_name"
    fi
done

# Cleanup
rm "$EXEC_FILE"
echo "--------------------------------"
echo "Testing complete."