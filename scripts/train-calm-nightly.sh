#!/usr/bin/env bash
set -euo pipefail

# Nightly CALM training script
# Runs the full three-phase CALM pipeline: LLC -> Encoder -> HLC (all tasks)
# Designed to run on the self-hosted runner via GitHub Actions

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Defaults (override via environment variables)
OUTPUT_DIR="${CALM_OUTPUT_DIR:-checkpoints/calm}"
MOTIONS_DIR="${CALM_MOTIONS_DIR:-data/calm/motions}"
DEVICE="${CALM_DEVICE:-auto}"
NUM_ENVS="${CALM_NUM_ENVS:-256}"
LLC_ITERATIONS="${CALM_LLC_ITERATIONS:-5000}"
ENCODER_ITERATIONS="${CALM_ENCODER_ITERATIONS:-5000}"
HLC_ITERATIONS="${CALM_HLC_ITERATIONS:-2000}"
SEED="${CALM_SEED:-42}"

echo "========================================"
echo "CALM Nightly Training"
echo "========================================"
echo "Output:             $OUTPUT_DIR"
echo "Motions:            $MOTIONS_DIR"
echo "Device:             $DEVICE"
echo "Num envs:           $NUM_ENVS"
echo "LLC iterations:     $LLC_ITERATIONS"
echo "Encoder iterations: $ENCODER_ITERATIONS"
echo "HLC iterations:     $HLC_ITERATIONS"
echo "Seed:               $SEED"
echo "========================================"

# Phase 1: LLC Training (PPO + AMP)
echo ""
echo "[Phase 1/3] Training LLC..."
python -m tools.calm_training.train_llc \
    --output "$OUTPUT_DIR" \
    --motions "$MOTIONS_DIR" \
    --iterations "$LLC_ITERATIONS" \
    --num-envs "$NUM_ENVS" \
    --device "$DEVICE" \
    --seed "$SEED"

LLC_CHECKPOINT="$OUTPUT_DIR/llc_checkpoint_$(printf '%06d' "$LLC_ITERATIONS").pt"
if [ ! -f "$LLC_CHECKPOINT" ]; then
    echo "ERROR: LLC checkpoint not found at $LLC_CHECKPOINT"
    exit 1
fi
echo "[Phase 1/3] LLC training complete. Checkpoint: $LLC_CHECKPOINT"

# Phase 2: Encoder Training (Contrastive)
echo ""
echo "[Phase 2/3] Training Encoder..."
python -m tools.calm_training.train_encoder \
    --llc-checkpoint "$LLC_CHECKPOINT" \
    --output "$OUTPUT_DIR" \
    --motions "$MOTIONS_DIR" \
    --iterations "$ENCODER_ITERATIONS" \
    --device "$DEVICE" \
    --seed "$SEED"

echo "[Phase 2/3] Encoder training complete."

# Phase 3: HLC Training (all tasks)
echo ""
echo "[Phase 3/3] Training HLCs..."
for task in heading location strike; do
    echo "  Training HLC: $task"
    python -m tools.calm_training.train_hlc \
        --task "$task" \
        --llc-checkpoint "$LLC_CHECKPOINT" \
        --output "$OUTPUT_DIR" \
        --motions "$MOTIONS_DIR" \
        --iterations "$HLC_ITERATIONS" \
        --num-envs "$NUM_ENVS" \
        --device "$DEVICE" \
        --seed "$SEED"
    echo "  HLC $task complete."
done

# Verify all exported weights
echo ""
echo "Verifying exported weights..."
EXPECTED_FILES=(
    "llc_style.bin"
    "llc_main.bin"
    "llc_mu_head.bin"
    "encoder.bin"
    "hlc_heading.bin"
    "hlc_location.bin"
    "hlc_strike.bin"
)

VERIFY_FAILED=0
for f in "${EXPECTED_FILES[@]}"; do
    if [ ! -f "$OUTPUT_DIR/$f" ]; then
        echo "  MISSING: $OUTPUT_DIR/$f"
        VERIFY_FAILED=1
    else
        python -c "from tools.ml.export import verify_weights; verify_weights('$OUTPUT_DIR/$f')"
        echo "  OK: $f"
    fi
done

if [ "$VERIFY_FAILED" -ne 0 ]; then
    echo "ERROR: Some weight files are missing!"
    exit 1
fi

echo ""
echo "========================================"
echo "CALM nightly training complete!"
echo "========================================"
