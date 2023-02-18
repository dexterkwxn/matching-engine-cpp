#!/usr/bin/env bash

make clean && make -j8
for filename in tests/*; do
  echo ""
  echo ""
  echo "Testing $filename"
  ./grader engine < "$filename"
done

