#! /bin/sh

set -e

function fix_links() {
  sed -i -re '
      # Mark all links so we can selectively fix them.
      s,<a href="([^"]*)">,<a maybehref="\1">,g;

      # Inject a % after every </a> to get around sed`s apparent lack of
      # non-greedy matching.  We`ll remove it later.
      s,</a>,</a>%,g

      # Remove links to internal package.
      s,<a maybehref="google\.protobuf\.internal[^"]*">([^%]*)</a>%,\1,g;

      # Remove links to unittest stuff.  Don`t even write the link text.
      s,<a maybehref="google\.protobuf\.unittest[^"]*">[^%]*</a>%,,g;

      # Accept google.protobuf links.
      s,<a maybehref="(google\.protobuf\.[^"]*)">,<a href="\1">,g;

      # Remove remaining links.
      s,<a maybehref="[^"]*">([^%]*)</a>%,\1,g;

      # google.protobuf.html -> index.html
      s,google\.protobuf\.html,index.html,g;

      # Remove that % from earlier.
      s,</a>%,</a>,g

      # Remove directory name.
      s,/[^<>]*/(google/protobuf/[a-zA-Z0-9_]+\.py),\1,g;

      # Fix the hideous background color.
      s,bgcolor="#f0f0f8",bgcolor="#ffffff",g
    ' $1
}

export PYTHONPATH=$PWD/../python

mkdir -p gcode/python
cd gcode/python

pydoc -w google.protobuf

fix_links google.protobuf.html
mv google.protobuf.html index.html

for FILE in $PYTHONPATH/google/protobuf/*.py; do
  BASENAME=`basename $FILE .py`
  case $BASENAME in
    __init__ | unittest* )
      # ignore
      ;;
    *)
      pydoc -w google.protobuf.$BASENAME
      fix_links google.protobuf.$BASENAME.html
      ;;
  esac
done
