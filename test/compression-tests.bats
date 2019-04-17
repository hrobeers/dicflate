#!/usr/bin/env bats

BD=${BATS_TEST_DIRNAME}/..
DD=${BATS_TEST_DIRNAME}/data

@test "deflate: no dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/dicflate \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/dicflate \
                 | ${BD}/dicflate -x \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "deflate: with dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/dicflate -d ${DD}/generated.single.json \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/dicflate -d ${DD}/generated.single.json \
                 | ${BD}/dicflate -x -d ${DD}/generated.single.json \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "deflate: comparison dict/no-dict" {
    size=$(cat ${DD}/generated.single.json | wc -c)
    compressed_dict=$(cat ${DD}/generated.single.json \
                      | ${BD}/dicflate -d ${DD}/generated.json \
                      | wc -c)
    compressed_nodict=$(cat ${DD}/generated.single.json \
                        | ${BD}/dicflate \
                        | wc -c)

    # Verify
    [ $compressed_nodict -lt $size ]
    [ $compressed_dict -lt $compressed_nodict ]
}
