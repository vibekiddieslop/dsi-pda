#!/bin/bash
# Rebuilds data/words.bin, the swipe-typing word list, from SCOWL (Ubuntu
# package "scowl", license in third_party/scowl/COPYRIGHT). One word per line,
# prefixed by a rank digit: 0 = SCOWL size 10 (most common), 1 = size 20,
# 2 = size 35. American spelling, 2 to 15 characters: lowercase words,
# capitalized words (Friday, January, English) and contractions (don't).
#   sudo apt-get install scowl && bash tools/make_words.sh
set -euo pipefail
D=/usr/share/dict/scowl
OUT="$(cd "$(dirname "$0")/.." && pwd)/data/words.bin"
tmp=$(mktemp)
rank=0
for size in 10 20 35; do
  for kind in words upper contractions; do
    for f in english-$kind.$size american-$kind.$size; do
      [ -f "$D/$f" ] || continue
      # Possessives ("abbey's") only bloat the list; "it's" comes from the
      # contractions lists.
      skip="'s\$"
      [ $kind = contractions ] && skip='^$'
      iconv -f latin1 -t utf-8 "$D/$f" | grep -E "^[A-Za-z][a-z']{0,13}[a-z]$" \
        | grep -vE "$skip" | sed "s/^/$rank /" >> "$tmp"
    done
  done
  rank=$((rank+1))
done
# Planner words SCOWL ranks lower than a to-do list uses them.
for w in Monday Tuesday Wednesday Thursday Friday Saturday Sunday January February \
         March April May June July August September October November December \
         I\'m I\'ll I\'ve I\'d; do
  echo "0 $w" >> "$tmp"
done
# Each word once (ignoring case), at its best rank; the lowercase form wins
# when both exist ("may" over "May"), so capitals stay only where needed.
awk '{ w = $2; k = tolower(w); r = $1
       if (!(k in rank) || r < rank[k]) rank[k] = r
       if (!(k in word) || w == k) word[k] = w }
     END { for (k in word) print rank[k] word[k] }' "$tmp" | LC_ALL=C sort > "$OUT"
rm -f "$tmp"
wc -l "$OUT"
