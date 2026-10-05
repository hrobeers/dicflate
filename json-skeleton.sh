#!/usr/bin/env bash
# json-skeleton.sh
#
# Reads a JSON document from stdin and emits the same document with every
# leaf-level value stripped (replaced by an empty string). All keys and
# structural characters (braces, brackets, quotes, colons, commas) in front
# of and behind each value are preserved, so the output is a "skeleton"
# of the document.
#
# Use it to build a deflate/preset dictionary: the skeleton contains all the
# repeated structural text (keys + delimiters) that deflate can then
# back-reference when compressing the real documents.
#
# Usage:
#   ./json-skeleton.sh < input.json > skeleton.json
#   ./json-skeleton.sh '["solved"]' < input.json > skeleton.json
#
# The optional first argument is a JSON array of blacklisted keys. A
# blacklisted key has its entire subtree removed as if it were a leaf:
# objects collapse to {}, arrays to []. Everything below it is dropped.
#
# Notes:
# - Leaf values (strings, numbers, booleans, null) become "".
# - Non-empty arrays are collapsed to a single skeleton element, since the
#   repeating array structure adds nothing new to the dictionary.
# - The blacklist is interpolated as a jq literal into the program, which
#   avoids jq flags (--arg/--argjson/-s) that some minimal jq builds lack.

set -eu
set -o pipefail

BL="${1:-[]}"

jq '
def bl: '"$BL"';
def skeleton:
  if type == "object" then
    reduce (keys_unsorted[]) as $k (.;
      .[$k] = (if (bl | index($k)) then
                 (if .[$k] | type == "array" then [] else {} end)
               elif .[$k] | type == "object" then (.[$k] | skeleton)
               elif .[$k] | type == "array" then (.[$k] | skeleton)
               else "" end))
  elif type == "array" then
    (if length == 0 then [] else [.[0] | skeleton] end)
  else
    ""
  end;
skeleton' -c
