rm -rf key/*
mkdir -p key
echo -n "1" > key/index
DIRS_5=()
DIRS_4=()
DIRS_3=()
DIRS_2=()
DIRS_1=()

for ots in $(seq 1 16); do
    for i in $(seq 1 256); do
        for bit in 0 1; do
            wolfssl rand -out _tmp_secret.bin 32
            xxd -p -c 32 _tmp_secret.bin > "${i}_${bit}_ots_secret.txt"
            rm -f _tmp_secret.bin
            wolfssl -hash -sha256 -in "${i}_${bit}_ots_secret.txt" > "${i}_${bit}_ots_public.txt"
        done
    done

    cat $(seq 1 256 | while read i; do echo "${i}_0_ots_public.txt"; echo "${i}_1_ots_public.txt"; done) > all_public.txt
    LEAF_HASH=$(wolfssl -hash -sha256 -in all_public.txt)
    mkdir -p "key/${LEAF_HASH}"
    mv *.txt "key/${LEAF_HASH}/"
    DIRS_5+=("${LEAF_HASH}")
done

for i in $(seq 0 2 14); do
    LEFT="${DIRS_5[$i]}"
    RIGHT="${DIRS_5[$((i+1))]}"
    echo -n "${LEFT}${RIGHT}" > _tmp_concat.txt
    PARENT_HASH=$(wolfssl -hash -sha256 -in _tmp_concat.txt)
    rm -f _tmp_concat.txt
    mkdir -p "key/${PARENT_HASH}"
    mv "key/${LEFT}" "key/${RIGHT}" "key/${PARENT_HASH}/"
    echo "LEFT=${LEFT}" > "key/${PARENT_HASH}/NODE.txt"
    echo "RIGHT=${RIGHT}" >> "key/${PARENT_HASH}/NODE.txt"
    DIRS_4+=("${PARENT_HASH}")
done

for i in $(seq 0 2 6); do
    LEFT="${DIRS_4[$i]}"
    RIGHT="${DIRS_4[$((i+1))]}"
    echo -n "${LEFT}${RIGHT}" > _tmp_concat.txt
    PARENT_HASH=$(wolfssl -hash -sha256 -in _tmp_concat.txt)
    rm -f _tmp_concat.txt
    mkdir -p "key/${PARENT_HASH}"
    mv "key/${LEFT}" "key/${RIGHT}" "key/${PARENT_HASH}/"
    echo "LEFT=${LEFT}" > "key/${PARENT_HASH}/NODE.txt"
    echo "RIGHT=${RIGHT}" >> "key/${PARENT_HASH}/NODE.txt"
    DIRS_3+=("${PARENT_HASH}")
done

for i in $(seq 0 2 2); do
    LEFT="${DIRS_3[$i]}"
    RIGHT="${DIRS_3[$((i+1))]}"
    echo -n "${LEFT}${RIGHT}" > _tmp_concat.txt
    PARENT_HASH=$(wolfssl -hash -sha256 -in _tmp_concat.txt)
    rm -f _tmp_concat.txt
    mkdir -p "key/${PARENT_HASH}"
    mv "key/${LEFT}" "key/${RIGHT}" "key/${PARENT_HASH}/"
    echo "LEFT=${LEFT}" > "key/${PARENT_HASH}/NODE.txt"
    echo "RIGHT=${RIGHT}" >> "key/${PARENT_HASH}/NODE.txt"
    DIRS_2+=("${PARENT_HASH}")
done

for i in $(seq 0 2 0); do
    LEFT="${DIRS_2[$i]}"
    RIGHT="${DIRS_2[$((i+1))]}"
    echo -n "${LEFT}${RIGHT}" > _tmp_concat.txt
    PARENT_HASH=$(wolfssl -hash -sha256 -in _tmp_concat.txt)
    rm -f _tmp_concat.txt
    mkdir -p "key/${PARENT_HASH}"
    mv "key/${LEFT}" "key/${RIGHT}" "key/${PARENT_HASH}/"
    echo "LEFT=${LEFT}" > "key/${PARENT_HASH}/NODE.txt"
    echo "RIGHT=${RIGHT}" >> "key/${PARENT_HASH}/NODE.txt"
    DIRS_1+=("${PARENT_HASH}")
done

# DIRS_1 holds the single root hash: the public key.
echo -n "${DIRS_1[0]}" > key/root.txt
echo "public key (Merkle root): ${DIRS_1[0]}"
