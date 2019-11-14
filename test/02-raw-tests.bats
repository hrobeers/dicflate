#!/usr/bin/env bats

BD=${BATS_TEST_DIRNAME}/..
DD=${BATS_TEST_DIRNAME}/data

@test "raw deflate: regression test" {
    result=$(echo "hello world" | ${BD}/dicflate -r  | sha1sum | awk '{print $1}')

    # Verify
    [ $result == "0b6d6a956774d5559c0d5f3b0625bb295c281229" ]
}

@test "raw deflate: no dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/dicflate -r \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/dicflate -r \
                 | ${BD}/dicflate -rx \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "raw deflate: with dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/dicflate -r -d ${DD}/generated.single.json \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/dicflate -r -d ${DD}/generated.single.json \
                 | ${BD}/dicflate -r -x -d ${DD}/generated.single.json \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "raw deflate: comparison dict/no-dict" {
    size=$(cat ${DD}/generated.single.json | wc -c)
    compressed_dict=$(cat ${DD}/generated.single.json \
                      | ${BD}/dicflate -r -d ${DD}/generated.json \
                      | wc -c)
    compressed_nodict=$(cat ${DD}/generated.single.json \
                        | ${BD}/dicflate -r \
                        | wc -c)

    # Verify
    [ $compressed_nodict -lt $size ]
    [ $compressed_dict -lt $compressed_nodict ]
}

@test "raw deflate: smaller than zlib deflate" {
    size=$(cat ${DD}/generated.single.json | wc -c)
    compressed_raw=$(cat ${DD}/generated.single.json \
                          | ${BD}/dicflate -r \
                          | wc -c)
    compressed_zlib=$(cat ${DD}/generated.single.json \
                            | ${BD}/dicflate \
                            | wc -c)

    # Verify
    [ $compressed_zlib -lt $size ]
    [ $compressed_raw -lt $compressed_zlib ]
}
