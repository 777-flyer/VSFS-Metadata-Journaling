#!/bin/bash

echo "=== Comprehensive Journal Test ==="

# Test 1: Basic functionality
echo "Test 1: Basic create and install"
./mkfs
./journal create test1.txt
./journal install
./validator || echo "FAILED: Test 1"

# Test 2: Multiple files before install
echo "Test 2: Multiple creates before install"
./mkfs
./journal create file1.txt
./journal create file2.txt
./journal create file3.txt
./journal install
./validator || echo "FAILED: Test 2"

# Test 3: Long filename
echo "Test 3: Long filename (27 chars)"
./mkfs
./journal create "abcdefghijklmnopqrstuvwxy"
./journal install
./validator || echo "FAILED: Test 3"

# Test 4: Many files
echo "Test 4: Creating 20 files"
./mkfs
for i in {1..20}; do
  ./journal create "file$i.txt"
  if [ $((i % 5)) -eq 0 ]; then
    ./journal install
  fi
done
./journal install
./validator || echo "FAILED: Test 4"

# Test 5: Boundary test (around inode 32)
echo "Test 5: Boundary test (inode blocks)"
./mkfs
for i in {1..35}; do
  ./journal create "b$i.txt"
done
./journal install
./validator || echo "FAILED: Test 5"

echo "=== All tests complete ==="