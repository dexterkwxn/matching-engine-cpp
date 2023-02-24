#!/usr/bin/env bash

make -j8
for filename in tests/*; do
  echo ""
  echo ""
  echo "Testing $filename"
  ./grader engine < "$filename"
done

