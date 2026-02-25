#!/usr/bin/env bash
set -euo pipefail

# Nightly UniCon training script
# Runs the full UniCon PPO training pipeline
# Designed to run on the self-hosted runner via GitHub Actions

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Defaults (override via environment variables)
OUTPUT_DIR="${UNICON_OUTPUT_DIR:-generated/unicon}"
MOTIONS_DIR="${UNICON_MOTIONS_DIR:-assets/motions}"
DEVICE="${UNICON_DEVICE:-auto}"
NUM_ENVS="${UNICON_NUM_ENVS:-256}"
ITERATIONS="${UNICON_ITERATIONS:-10000}"
SEED="${UNICON_SEED:-42}"

echo "========================================"
echo "UniCon Nightly Training"
echo "========================================"
echo "Output:     $OUTPUT_DIR"
echo "Motions:    $MOTIONS_DIR"
echo "Device:     $DEVICE"
echo "Num envs:   $NUM_ENVS"
echo "Iterations: $ITERATIONS"
echo "Seed:       $SEED"
echo "========================================"

echo ""
echo "Training UniCon policy..."
python -m tools.unicon_training.train \
    --output "$OUTPUT_DIR" \
    --motions "$MOTIONS_DIR" \
    --iterations "$ITERATIONS" \
    --num-envs "$NUM_ENVS" \
    --device "$DEVICE" \
    --seed "$SEED"

# Verify exported weights
echo ""
echo "Verifying exported weights..."
POLICY_BIN="$OUTPUT_DIR/policy_weights.bin"
if [ ! -f "$POLICY_BIN" ]; then
    echo "ERROR: Policy weights not found at $POLICY_BIN"
    exit 1
fi

python -c "from tools.ml.export import verify_weights; verify_weights('$POLICY_BIN')"
echo "  OK: policy_weights.bin"

echo ""
echo "========================================"
echo "UniCon nightly training complete!"
echo "========================================"
