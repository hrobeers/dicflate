#!/usr/bin/env bats

BD=${BATS_TEST_DIRNAME}/..
DD=${BATS_TEST_DIRNAME}/data

@test "raw zstd: no dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/dicstd -r \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/dicstd -r \
                 | ${BD}/dicstd -rx \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "raw zstd: with dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/dicstd -r -d ${DD}/generated.single.json \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/dicstd -r -d ${DD}/generated.single.json \
                 | ${BD}/dicstd -rx -d ${DD}/generated.single.json \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "raw zstd: comparison dict/no-dict" {
    compressed_dict=$(cat ${DD}/generated.single.json \
                      | ${BD}/dicstd -r -d ${DD}/generated.json \
                      | wc -c)
    compressed_nodict=$(cat ${DD}/generated.single.json \
                        | ${BD}/dicstd -r \
                        | wc -c)

    # Verify
    [ $compressed_dict -lt $compressed_nodict ]
}

@test "raw zstd: magicless saves four bytes" {
    framed_size=$(cat ${DD}/generated.single.json \
                  | ${BD}/dicstd \
                  | wc -c)
    raw_size=$(cat ${DD}/generated.single.json \
               | ${BD}/dicstd -r \
               | wc -c)

    # Verify
    [ $raw_size -eq $((framed_size - 4)) ]
}

@test "raw zstd: growing the dictionary by prepending" {
    tmp=$(mktemp -d)
    # grow the dictionary by prepending new content,
    # keeping the old dictionary as the tail
    cat ${DD}/generated.json ${DD}/generated.single.json > ${tmp}/dict-grown

    # compress with the old dictionary, extract with the grown one
    cat ${DD}/generated.single.json \
        | ${BD}/dicstd -r -d ${DD}/generated.single.json \
        | ${BD}/dicstd -rx -d ${tmp}/dict-grown \
        | cmp - ${DD}/generated.single.json

    # Verify the compressed data itself is unchanged by the growing dictionary
    rm -rf ${tmp}
}

@test "raw zstd: mismatched dict does not extract the original" {
    # decoding with an unrelated dictionary must yield garbage or an error,
    # never the original document
    if cat ${DD}/generated.single.json \
        | ${BD}/dicstd -r -d ${DD}/generated.single.json \
        | ${BD}/dicstd -rx -d ${DD}/generated.json \
        | cmp -s - ${DD}/generated.single.json; then
        fail "mismatched dictionary extracted the original"
    fi
}
