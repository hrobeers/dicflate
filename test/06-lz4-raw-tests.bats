#!/usr/bin/env bats

BD=${BATS_TEST_DIRNAME}/..
DD=${BATS_TEST_DIRNAME}/data

@test "raw lz4: no dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/diclz4 -r \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/diclz4 -r \
                 | ${BD}/diclz4 -rx \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "raw lz4: with dict" {
    size=$(cat ${DD}/generated.json | wc -c)
    compressed_size=$(cat ${DD}/generated.json \
                      | ${BD}/diclz4 -r -d ${DD}/generated.single.json \
                      | wc -c)
    result_size=$(cat ${DD}/generated.json \
                 | ${BD}/diclz4 -r -d ${DD}/generated.single.json \
                 | ${BD}/diclz4 -rx -d ${DD}/generated.single.json \
                 | wc -c)

    # Verify
    [ $compressed_size -lt $size ]
    [ $result_size == $size ]
}

@test "raw lz4: comparison dict/no-dict" {
    compressed_dict=$(cat ${DD}/generated.single.json \
                      | ${BD}/diclz4 -r -d ${DD}/generated.json \
                      | wc -c)
    compressed_nodict=$(cat ${DD}/generated.single.json \
                        | ${BD}/diclz4 -r \
                        | wc -c)

    # Verify
    [ $compressed_dict -lt $compressed_nodict ]
}

@test "raw lz4: no magic bytes" {
    tmp=$(mktemp -d)
    cat ${DD}/generated.single.json | ${BD}/diclz4 -r > ${tmp}/raw
    magic=$(head -c 4 ${tmp}/raw | od -An -tx1 | tr -d ' \n')

    # Verify: not the lz4 frame magic number (04 22 4d 18)
    [ "$magic" != "04224d18" ]
    rm -rf ${tmp}
}

@test "raw lz4: not decodable by the lz4 cli" {
    tmp=$(mktemp -d)
    cat ${DD}/generated.single.json | ${BD}/diclz4 -r > ${tmp}/raw

    # Verify: the lz4 cli must reject the magicless stream
    if lz4 -d -c < ${tmp}/raw > /dev/null 2>&1; then
        fail "lz4 cli accepted a raw diclz4 stream"
    fi
    rm -rf ${tmp}
}

@test "raw lz4: growing the dictionary by prepending" {
    tmp=$(mktemp -d)
    # grow the dictionary by prepending new content,
    # keeping the old dictionary as the tail
    cat ${DD}/generated.json ${DD}/generated.single.json > ${tmp}/dict-grown

    # compress with the old dictionary, extract with the grown one
    cat ${DD}/generated.single.json \
        | ${BD}/diclz4 -r -d ${DD}/generated.single.json \
        | ${BD}/diclz4 -rx -d ${tmp}/dict-grown \
        | cmp - ${DD}/generated.single.json
    rm -rf ${tmp}
}

@test "raw lz4: mismatched dict does not extract the original" {
    # decoding with an unrelated dictionary must yield garbage or an error,
    # never the original document
    if cat ${DD}/generated.single.json \
        | ${BD}/diclz4 -r -d ${DD}/generated.single.json \
        | ${BD}/diclz4 -rx -d ${DD}/generated.json \
        | cmp -s - ${DD}/generated.single.json; then
        fail "mismatched dictionary extracted the original"
    fi
}

@test "raw lz4: multi-block round trip with dict" {
    tmp=$(mktemp -d)
    # more than one 1 MiB block, to exercise the window across blocks
    for i in $(seq 256); do cat ${DD}/generated.json; done > ${tmp}/big
    ${BD}/diclz4 -r -d ${DD}/generated.single.json < ${tmp}/big \
        | ${BD}/diclz4 -rx -d ${DD}/generated.single.json \
        | cmp - ${tmp}/big
    rm -rf ${tmp}
}

@test "raw lz4: incompressible blocks round trip" {
    tmp=$(mktemp -d)
    # random data cannot be compressed: blocks are stored raw
    dd if=/dev/urandom of=${tmp}/rand bs=1M count=2 status=none
    ${BD}/diclz4 -r -d ${DD}/generated.single.json < ${tmp}/rand \
        | ${BD}/diclz4 -rx -d ${DD}/generated.single.json \
        | cmp - ${tmp}/rand
    rm -rf ${tmp}
}
