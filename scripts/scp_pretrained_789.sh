#!/usr/bin/env bash
# Download verified seed0/count004 datasets; nohup/setsid launch survives logout.
set -euo pipefail
trap '' HUP
DEST_DIR="${1:?Usage: bash scripts/scp_pretrained_789.sh DEST_DIR}"
mkdir -p "$DEST_DIR"
cd "$DEST_DIR"
echo "$$" > PID
finish() {
 local rc=$?
 if [[ $rc -eq 0 ]]; then
  echo "COMPLETED finished=$(date -Iseconds)" > STATUS
 else
  echo "FAILED exit_code=$rc finished=$(date -Iseconds)" > STATUS
 fi
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
cat > SHA256SUMS <<'SUMS'
74ebe38e44810ef8602bafd234761a8403b842dc3045a43a7873d807c6bff2ea  tuples7-seed0-VSE-count004.dat.xz
3c0b41df8f3e5f04f1429afe89c252ac5349fb93329614fa69cedb6bbbe6baf5  tuples8-seed0-VSE-count004.dat.xz
92d40906b5e46822e34545e64bf6053d7d59159194828297a35b4eb417ba774a  tuples9-seed0-VSE-count004.dat.xz
SUMS
for tuple in 7 8 9; do
 file="tuples${tuple}-seed0-VSE-count004.dat.xz"
 echo "RUNNING phase=scp file=$file started=$(date -Iseconds)" > STATUS
 echo "Downloading $file"
 /usr/bin/scp -p -P 1979 -o BatchMode=yes -o StrictHostKeyChecking=yes \
  -o ConnectTimeout=20 -o ServerAliveInterval=30 -o ServerAliveCountMax=6 \
  "matsu-lab@222.229.71.138:/home2/matsu-lab2/terauchi/2048_NewTypeTuple/learn_double/dat/$file" "$file.part"
 mv "$file.part" "$file"
 # Verify the downloaded bytes before expanding them.
 awk -v file="$file" '$2==file' SHA256SUMS | sha256sum -c -
 echo "RUNNING phase=decompress file=$file started=$(date -Iseconds)" > STATUS
 echo "Decompressing $file"
 xz -d -k -T1 -- "$file"
done
sha256sum -c SHA256SUMS
echo "All three datasets downloaded, verified and decompressed."
