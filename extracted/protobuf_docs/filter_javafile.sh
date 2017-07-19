#!/bin/bash
# Filters out public java files for javadocs.

cat protobuf/Makefile.am | awk '
  BEGIN {
    show = 0
  }
  /^java_EXTRA_DIST/ {
    show = 1
    next
  }
  /^ *$/ {
    show = 0
  }
  show {
    print $0
  }' \
| grep "\.java" | grep -v 'src/test' |
  grep -v GeneratedMessage | grep -v Lazy | grep -v MapEntry |
  grep -v FieldBuilder | grep -v UnknownField | grep -v Internal |
  sed -re 's,^ *,  protobuf/,g'
