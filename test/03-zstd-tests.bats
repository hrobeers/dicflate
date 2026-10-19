#!/usr/bin/env bats

BD=${BATS_TEST_DIRNAME}/..
DD=${BATS_TEST_DIRNAME}/data

@test "zstd: no dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/dicstd \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/dicstd \
                 | ${BD}/dicstd -x \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "zstd: with dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/dicstd -d ${DD}/generated.single.json \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/dicstd -d ${DD}/generated.single.json \
                 | ${BD}/dicstd -x -d ${DD}/generated.single.json \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "zstd: comparison dict/no-dict" {
    size=$(cat ${DD}/generated.single.json | wc -c)
    compressed_dict=$(cat ${DD}/generated.single.json \
                      | ${BD}/dicstd -d ${DD}/generated.json \
                      | wc -c)
    compressed_nodict=$(cat ${DD}/generated.single.json \
                        | ${BD}/dicstd \
                        | wc -c)

    # Verify
    [ $compressed_nodict -lt $size ]
    [ $compressed_dict -lt $compressed_nodict ]
}

@test "zstd: interop with the zstd cli" {
    tmp=$(mktemp -d)
    cat ${DD}/generated.json \
        | ${BD}/dicstd \
        | zstd -d -c > ${tmp}/out

    # Verify
    cmp ${tmp}/out ${DD}/generated.json
    rm -rf ${tmp}
}
