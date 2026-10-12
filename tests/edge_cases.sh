#!/bin/bash
# Run from the project folder after building: g++ -std=c++17 server.cpp -o server
# Usage: bash tests/edge_cases.sh
# Each test prints the server message and the exit code.


run_test()
{
    local testName="$1"
    local programText="$2"

    printf "$programText" > source.bin

    echo "--- $testName"
    ./server

    local exitCode=$?
    echo "exit=$exitCode"
}

make_param_names()
{
    local count="$1"
    local result=""

    for (( i = 1; i <= count; i++ ))
    do
        result="$result a$i"
    done

    echo "$result"
}

make_numbers()
{
    local count="$1"
    local result=""

    for (( i = 1; i <= count; i++ ))
    do
        result="$result $i"
    done

    echo "$result"
}

#input issues
run_test "empty file (expect: no main, exit=1)" ""
run_test "windows line endings (expect: exit=0)" "func main\r\nset a 1\r\nfunc_end\r\n"
run_test "blank lines and tabs (expect: exit=0)" "\n  func main  \n\n\tset a\t1\n func_end\n"

#structure issues
run_test "no main (expect: no main, exit=1)" "func foo a\nfunc_end\n"
run_test "nested func (expect: invalid program, exit=1)" "func main\nfunc foo\nfunc_end\nfunc_end\n"
run_test "duplicate function (expect: resolve failed, exit=1)" "func f\nfunc_end\nfunc f\nfunc_end\nfunc main\nfunc_end\n"
run_test "undefined call (expect: undefined function, exit=1)" "func main\ncall xyz\nfunc_end\n"

#execution issues
run_test "unknown instruction (expect: bad line, exit=1)" "func main\njump a\nfunc_end\n"
run_test "divide by zero (expect: divide by zero, exit=1)" "func main\nset a 5\ndiv a 0\nfunc_end\n"
run_test "unset variable (expect: variable used before set, exit=1)" "func main\nadd x 1\nfunc_end\n"
run_test "arg count mismatch (expect: different number, exit=1)" "func f a b\nfunc_end\nfunc main\ncall f 1\nfunc_end\n"
run_test "recursion (expect: stack too deep, exit=1)" "func f\ncall f\nfunc_end\nfunc main\ncall f\nfunc_end\n"

#limits
params=$(make_param_names 16)
numbers=$(make_numbers 16)
run_test "16 params (expect: exit=0)" "func f $params\nfunc_end\nfunc main\ncall f $numbers\nfunc_end\n"

params=$(make_param_names 17)
numbers=$(make_numbers 17)
run_test "17 params (expect: bad line, exit=1)" "func f $params\nfunc_end\nfunc main\ncall f $numbers\nfunc_end\n"

echo "--- 17 variables (expect: more than 16 variables, exit=1)"

echo "func main" > source.bin

for (( i = 1; i <= 17; i++ ))
do
    echo "set v$i 1" >> source.bin
done

echo "func_end" >> source.bin

./server
echo "exit=$?"

rm -f source.bin resolve.bin session.tdbg