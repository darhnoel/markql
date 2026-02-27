#!/bin/bash
if [ "$1" = "explore" ]; then
  ./build/xsql "$@"
  exit $?
fi

for arg in "$@"; do
  case "$arg" in
    --query|--query-file|--interactive|-q)
      ./build/xsql "$@"
      exit $?
      ;;
  esac
done

./build/xsql --interactive "$@"
