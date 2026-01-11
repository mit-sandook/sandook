#!/bin/bash
set -ex

USAGE="usage: $0 <block_size_bytes> <pci_bdf> [<pci_bdf> ...]

Example:
  ./scripts/trim_nvme.sh 4096 81:00.0 82:00.0
"

SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../

if [ "$#" -lt 2 ]; then
  echo "$USAGE"
  exit 1
fi

BLOCK_SIZE_BYTES=$1
shift

PCIS=("$@")

# Ensure NVMe devices are not bound to SPDK before trimming.
bash "${SCRIPT_DIR}/reset.sh"

function pci_to_ns() {
  local pci=$1
  # nvme list -v prints a table that includes the PCI Address and Namespaces.
  # Example row:
  # nvme1  ...  pcie  0000:81:00.0  ...  nvme1n1
  sudo nvme list -v | awk -v pci="$pci" '
    # Match both 81:00.0 and 0000:81:00.0
    ($0 ~ ("0000:" pci) || $0 ~ (" " pci " ")) && $0 ~ /pcie/ {
      print $NF;
      exit;
    }'
}

for pci in "${PCIS[@]}"; do
  ns=$(pci_to_ns "$pci")
  if [ -z "$ns" ]; then
    sudo nvme list -v | head -n 120 || true
    echo "Could not find NVMe namespace for pci: $pci"
    exit 1
  fi

  dev="/dev/${ns}"
  if [ ! -e "$dev" ]; then
    sudo nvme list -v | head -n 120 || true
    echo "Resolved namespace $ns but device node does not exist: $dev"
    exit 1
  fi

  echo "Trimming pci=$pci dev=$dev"

  # Prefer --block-size if supported; otherwise try to select an LBAF with 4K (lbads:12).
  if sudo nvme format -h 2>&1 | grep -q "block-size"; then
    sudo nvme format "$dev" --block-size="$BLOCK_SIZE_BYTES" --force
  else
    LBAF=$(sudo nvme id-ns -H "$dev" 2>/dev/null | awk '($1=="lbaf" && $3==":" && $0 ~ /lbads:12/){print $2; exit}')
    if [ -n "$LBAF" ]; then
      sudo nvme format "$dev" --lbaf="$LBAF" --force
    else
      echo "Warning: could not find 4K LBAF (lbads:12) for $dev; skipping nvme format"
    fi
  fi

  sudo blkdiscard "$dev"
done


