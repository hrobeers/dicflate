#!/usr/bin/env bats

BD=${BATS_TEST_DIRNAME}/..
DD=${BATS_TEST_DIRNAME}/data

@test "lz4: no dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/diclz4 \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/diclz4 \
                 | ${BD}/diclz4 -x \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "lz4: with dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/diclz4 -d ${DD}/generated.single.json \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/diclz4 -d ${DD}/generated.single.json \
                 | ${BD}/diclz4 -x -d ${DD}/generated.single.json \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "lz4: comparison dict/no-dict" {
    compressed_dict=$(cat ${DD}/generated.single.json \
                      | ${BD}/diclz4 -d ${DD}/generated.json \
                      | wc -c)
    compressed_nodict=$(cat ${DD}/generated.single.json \
                        | ${BD}/diclz4 \
                        | wc -c)

    # Verify
    [ $compressed_dict -lt $compressed_nodict ]
}

@test "lz4: interop with the lz4 cli" {
    tmp=$(mktemp -d)
    cat ${DD}/generated.json \
        | ${BD}/diclz4 \
        | lz4 -d -c > ${tmp}/out 2>/dev/null

    # Verify
    cmp ${tmp}/out ${DD}/generated.json
    rm -rf ${tmp}
}

@test "lz4: level 12 compresses better than level 0" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_l0=$(cat ${DD}/generated.json \
                   | ${BD}/diclz4 -l 0 \
                   | wc -c)
    compressed_l12=$(cat ${DD}/generated.json \
                     | ${BD}/diclz4 -l 12 \
                     | wc -c)

    # Verify
    [ $compressed_l12 -le $compressed_l0 ]
    [ $compressed_l12 -lt $size ]
}

@test "lz4: multi-block frame round trip with dict" {
    tmp=$(mktemp -d)
    # more than one 4 MiB block, to exercise linked blocks
    for i in $(seq 512); do cat ${DD}/generated.json; done > ${tmp}/big
    ${BD}/diclz4 -d ${DD}/generated.single.json < ${tmp}/big \
        | ${BD}/diclz4 -x -d ${DD}/generated.single.json \
        | cmp - ${tmp}/big
    rm -rf ${tmp}
}
