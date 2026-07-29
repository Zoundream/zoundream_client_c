#!/usr/bin/env bash
#
# Runs the zoundream_client over every .wav file in a directory, saving the full output of each
# run to its own log file and producing a summary report that compares the expected answers
# (taken from the file name, e.g. "01-sleep+burp.wav") with the answers the API actually returned.
#
# Usage: ./run_cries.sh [ENDPOINT_URL] [CRIES_DIR] [PER_FILE_TIMEOUT_SECONDS]
#
#   ENDPOINT_URL             default: https://stage-znd-eu.zoundream-api.com/audio
#   CRIES_DIR                default: ~/babyt/cries/momcozy-jul-24
#   PER_FILE_TIMEOUT_SECONDS default: 600 (safety cap so a hung request can't stall the batch)

set -u

ENDPOINT="${1:-https://stage-znd-eu.zoundream-api.com/audio}"
CRIES_DIR="${2:-$HOME/babyt/cries/momcozy-jul-24}"
PER_FILE_TIMEOUT="${3:-600}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT="$SCRIPT_DIR/zoundream_client"

if [[ ! -x "$CLIENT" ]]; then
    echo "Error: client binary not found or not executable: $CLIENT" >&2
    echo "Build it first with ./build.sh" >&2
    exit 1
fi
if [[ ! -d "$CRIES_DIR" ]]; then
    echo "Error: cries directory not found: $CRIES_DIR" >&2
    exit 1
fi

# One timestamped output directory per batch, so repeated runs don't clobber each other.
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="$SCRIPT_DIR/cry-runs/$STAMP"
mkdir -p "$OUT_DIR"
REPORT="$OUT_DIR/report.txt"

# Gather the wav files, sorted by name.
shopt -s nullglob
files=("$CRIES_DIR"/*.wav)
shopt -u nullglob
IFS=$'\n' files=($(sort <<<"${files[*]}")); unset IFS

if [[ ${#files[@]} -eq 0 ]]; then
    echo "No .wav files found in $CRIES_DIR" >&2
    exit 1
fi

echo "Endpoint:  $ENDPOINT"
echo "Cries dir: $CRIES_DIR"
echo "Output:    $OUT_DIR"
echo "Files:     ${#files[@]}"
echo

# Report header.
{
    echo "Zoundream cry batch report"
    echo "Generated: $STAMP"
    echo "Endpoint:  $ENDPOINT"
    echo "Cries dir: $CRIES_DIR"
    echo "Files:     ${#files[@]}"
    echo
    printf '%-34s | %-26s | %-30s | %s\n' "FILE" "EXPECTED (from name)" "DETECTED (valid answers)" "OUTCOME"
    printf '%s\n' "-----------------------------------------------------------------------------------------------------------------------------"
} > "$REPORT"

total=0
translated_any=0

for f in "${files[@]}"; do
    total=$((total + 1))
    name="$(basename "$f")"
    stem="${name%.wav}"
    # Expected answers: drop the leading "NN-" and turn "+" separators into commas.
    expected="$(echo "$stem" | sed -E 's/^[0-9]+-//' | tr '+' ',')"

    log="$OUT_DIR/$stem.log"
    echo "[$total/${#files[@]}] $name ..."

    timeout "$PER_FILE_TIMEOUT" "$CLIENT" "$ENDPOINT" "$f" > "$log" 2>&1
    rc=$?
    # zoundream_client exits with this specific code (see EXIT_AUTH_FAILED in api.h) the moment
    # the server rejects a request as unauthorized/forbidden, instead of continuing to send audio
    # that will never be accepted (authentication is either always rejected or always accepted for each key).
    if [[ $rc -eq 3 ]]; then
        echo
        echo "Error: authentication failed (bad or missing API key). Fix it in api.h and try again." >&2
        exit 1
    fi

    # Pull the answer out of every "done" response in the log.
    mapfile -t done_answers < <(grep '"phase":"done"' "$log" | grep -oE '"answer":"[a-z_]+"' | sed -E 's/"answer":"([a-z_]+)"/\1/')
    valid=()
    for a in "${done_answers[@]:-}"; do
        [[ -n "$a" && "$a" != "no_cry" ]] && valid+=("$a")
    done

    if [[ ${#valid[@]} -gt 0 ]]; then
        detected="$(IFS=,; echo "${valid[*]}")"
        outcome="translated"
        translated_any=$((translated_any + 1))
    else
        detected="(none)"
        outcome="no valid translation"
    fi
    if [[ $rc -eq 124 ]]; then
        outcome="$outcome (timed out after ${PER_FILE_TIMEOUT}s)"
    elif [[ $rc -ne 0 ]]; then
        outcome="$outcome (exit $rc)"
    fi

    printf '%-34s | %-26s | %-30s | %s\n' "$name" "$expected" "$detected" "$outcome" >> "$REPORT"
done

{
    echo
    echo "Summary: $translated_any/$total files produced at least one valid translation."
} >> "$REPORT"

echo
cat "$REPORT"
echo
echo "Per-file logs and this report are in: $OUT_DIR"
