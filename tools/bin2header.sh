#!/usr/bin/bash

input="$1"
output="$2"
var="$3"

echo "const char* ${var} =" > "${output}" &&
hexdump -v -e '16/1 "_x%02X" "\n"' "${input}" | sed 's/_/\\/g; s/\\x  //g; s/.*/    "&"/' >> "${output}" &&
echo ";" >> "${output}"
