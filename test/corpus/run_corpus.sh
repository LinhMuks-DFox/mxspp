#!/usr/bin/env bash
# Red/green corpus runner for MXScript (progress14 / task22).
#
#   GREEN cases (test/corpus/green/<name>.mxs + <name>.out): must run with rc 0 and produce
#   EXACTLY the bytes in <name>.out on stdout.
#
#   RED cases (test/corpus/red/<name>.mxs + <name>.err): must be REJECTED — rc must be non-zero
#   AND every line in <name>.err must appear as a substring of the program's combined std{out,err}
#   (so a red case fails for the RIGHT reason, not just any non-zero exit).
#
# Usage: test/corpus/run_corpus.sh [path-to-mxs]   (default: build/bin/mxs, searched upward)
# Exit code: 0 iff every case passes. CI-friendly; also wired into ctest (see test/CMakeLists.txt).
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$here/../.." && pwd)"

# Locate the mxs binary: explicit arg, then common build locations.
MXS="${1:-}"
if [[ -z "$MXS" ]]; then
    for cand in "$repo_root/build/bin/mxs" "$repo_root/bin/mxs" "build/bin/mxs"; do
        [[ -x "$cand" ]] && MXS="$cand" && break
    done
fi
if [[ -z "$MXS" || ! -x "$MXS" ]]; then
    echo "run_corpus: cannot find the mxs binary (pass it as \$1 or build first)" >&2
    exit 2
fi

# Run from the repo root so the import resolver's CWD-relative `./std/<m>.mxs` search path works.
cd "$repo_root" || exit 2

pass=0
fail=0
failed_names=()

green_one() {
    local mxs_file="$1" out_file="$2" name
    name="green/$(basename "$mxs_file")"
    local actual rc
    actual="$("$MXS" run-core "$mxs_file" 2>/dev/null)"
    rc=$?
    local expected
    expected="$(cat "$out_file")"
    if [[ $rc -eq 0 && "$actual" == "$expected" ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failed_names+=("$name")
        echo "FAIL $name  (rc=$rc)"
        if [[ $rc -ne 0 ]]; then
            echo "     expected rc 0; combined output:"
            "$MXS" run-core "$mxs_file" 2>&1 | sed 's/^/       /'
        else
            echo "     --- expected ---"; echo "$expected" | sed 's/^/       /'
            echo "     --- actual   ---"; echo "$actual"   | sed 's/^/       /'
        fi
    fi
}

red_one() {
    local mxs_file="$1" err_file="$2" name
    name="red/$(basename "$mxs_file")"
    local combined rc
    combined="$("$MXS" run-core "$mxs_file" 2>&1)"
    rc=$?
    if [[ $rc -eq 0 ]]; then
        fail=$((fail + 1)); failed_names+=("$name")
        echo "FAIL $name  (expected rejection, but rc=0 — silent pass)"
        echo "$combined" | sed 's/^/       /'
        return
    fi
    # Every non-empty line of the .err file must be a substring of the combined output.
    local missing=0 line
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        if [[ "$combined" != *"$line"* ]]; then
            missing=1
            echo "FAIL $name  (rc=$rc, but missing expected diagnostic):"
            echo "       want substring: $line"
        fi
    done < "$err_file"
    if [[ $missing -eq 0 ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1)); failed_names+=("$name")
        echo "     --- actual combined output ---"
        echo "$combined" | sed 's/^/       /'
    fi
}

shopt -s nullglob
for mxs_file in "$here"/green/*.mxs; do
    out_file="${mxs_file%.mxs}.out"
    if [[ ! -f "$out_file" ]]; then
        fail=$((fail + 1)); failed_names+=("green/$(basename "$mxs_file") [no .out]")
        echo "FAIL green/$(basename "$mxs_file")  (missing expected-output file $out_file)"
        continue
    fi
    green_one "$mxs_file" "$out_file"
done
for mxs_file in "$here"/red/*.mxs; do
    err_file="${mxs_file%.mxs}.err"
    if [[ ! -f "$err_file" ]]; then
        fail=$((fail + 1)); failed_names+=("red/$(basename "$mxs_file") [no .err]")
        echo "FAIL red/$(basename "$mxs_file")  (missing expected-diagnostic file $err_file)"
        continue
    fi
    red_one "$mxs_file" "$err_file"
done

echo "------------------------------------------------------------"
echo "corpus: $pass passed, $fail failed (of $((pass + fail)))"
if [[ $fail -ne 0 ]]; then
    printf '  failed: %s\n' "${failed_names[@]}"
    exit 1
fi
exit 0
