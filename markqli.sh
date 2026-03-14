#!/bin/bash
if [ "$1" = "explore" ]; then
  ./build/markql "$@"
  exit $?
fi

for arg in "$@"; do
  case "$arg" in
    --query|--query-file|--interactive|-q)
      ./build/markql "$@"
      exit $?
      ;;
  esac
done

./build/markql --interactive "$@"
