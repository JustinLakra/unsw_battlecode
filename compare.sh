#!/usr/bin/env bash
# Plays two bots against each other on every map, both ways round.
# usage: ./compare.sh v6 v5
set -u

NEW=${1:?usage: ./compare.sh NEW_BOT OLD_BOT}
OLD=${2:?usage: ./compare.sh NEW_BOT OLD_BOT}
NEW=${NEW%/} # allow "v6" or "v6/"
OLD=${OLD%/}

MAPS_DIR=v1/maps
OUT_DIR=replays/${NEW}-vs-${OLD}
RESULTS=$OUT_DIR/results.txt

mkdir -p "$OUT_DIR"
: >"$RESULTS" # start with an empty results file

for map in "$MAPS_DIR"/*.map; do
  name=$(basename "$map" .map)

  for order in "$NEW $OLD" "$OLD $NEW"; do
    read -r A B <<<"$order"
    tag="${A}-vs-${B}-${name}"

    echo "running $tag"
    unswbc run -o "$OUT_DIR/$tag.replay" "$map" "$A/" "$B/" \
      >"$OUT_DIR/$tag.log" 2>&1

    {
      echo "== $tag (Team A = $A, Team B = $B)"
      tail -n 2 "$OUT_DIR/$tag.log"
      echo
    } >>"$RESULTS"
  done
done

echo "done: see $RESULTS"
