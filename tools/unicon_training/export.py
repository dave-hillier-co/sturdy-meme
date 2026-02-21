#!/usr/bin/env python3
"""UniCon weight export CLI.

Thin wrapper around tools.ml.export with UniCon-specific defaults
(obs_dim=429 for 20-joint tau=1, act_dim=60 for 20 joints * 3 torques).

Usage:
    python -m tools.unicon_training.export --random --output generated/unicon/policy_weights.bin
    python -m tools.unicon_training.export --checkpoint generated/unicon/checkpoint_001000.pt
"""

import argparse

from tools.ml.export import export_policy, export_random_policy, verify_weights


# UniCon defaults: 20 joints, tau=1
# obs_dim = (1 + 1) * (11 + 10*20) + 1 * 7 = 429
# act_dim = 20 * 3 = 60
DEFAULT_OBS_DIM = 429
DEFAULT_ACT_DIM = 60


def main():
    parser = argparse.ArgumentParser(description="Export UniCon policy weights for C++")
    parser.add_argument("--checkpoint", type=str, default=None,
                        help="Path to training checkpoint (.pt)")
    parser.add_argument("--random", action="store_true",
                        help="Generate random weights (no deps required)")
    parser.add_argument("--obs-dim", type=int, default=DEFAULT_OBS_DIM,
                        help=f"Observation dimension (default: {DEFAULT_OBS_DIM})")
    parser.add_argument("--act-dim", type=int, default=DEFAULT_ACT_DIM,
                        help=f"Action dimension (default: {DEFAULT_ACT_DIM})")
    parser.add_argument("--hidden-dim", type=int, default=1024)
    parser.add_argument("--hidden-layers", type=int, default=3)
    parser.add_argument("--output", type=str, default="generated/unicon/policy_weights.bin")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    if args.random:
        export_random_policy(
            args.obs_dim, args.act_dim, args.output,
            args.hidden_dim, args.hidden_layers, args.seed,
        )
    elif args.checkpoint:
        import torch
        from tools.ml.config import PolicyConfig
        from tools.ml.policy import MLPPolicy

        checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
        config = PolicyConfig(hidden_dim=args.hidden_dim, hidden_layers=args.hidden_layers)
        policy = MLPPolicy(args.obs_dim, args.act_dim, config)
        policy.load_state_dict(checkpoint["policy_state_dict"])
        export_policy(policy, args.output)
    else:
        parser.error("Must specify either --checkpoint or --random")

    if args.verify:
        verify_weights(args.output)


if __name__ == "__main__":
    main()
