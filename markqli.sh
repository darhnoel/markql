#!/bin/bash
if [ "$1" = "explore" ]; then
  ./build/xsql "$@"
  exit $?
fi

./build/xsql --interactive "$@"
