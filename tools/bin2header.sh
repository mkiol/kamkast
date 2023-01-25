#!/usr/bin/bash

input="$1"
var="$2"

echo "const char* ${var} ="
hexdump -v -e '16/1 "_x%02X" "\n"' "${input}" | sed 's/_/\\/g; s/\\x  //g; s/.*/    "&"/'
echo ";"
