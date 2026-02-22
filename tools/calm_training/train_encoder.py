#!/usr/bin/env python3
"""CALM Phase 2: Encoder training via contrastive learning.

Trains a motion encoder that maps stacked observations (10 frames) to
L2-normalized 64D latent vectors. Uses InfoNCE contrastive loss:
  - Positive pairs: observations within `positive_window` frames of the
    same clip
  - Negative pairs: observations from different clips

The LLC is frozen during encoder training (loaded from Phase 1 checkpoint).

Usage:
    python -m tools.calm_training.train_encoder \\
        --llc-checkpoint checkpoints/calm/llc_checkpoint_005000.pt \\
        --motions data/calm/motions \\
        [--iterations 5000] [--device auto] \\
        [--output checkpoints/calm]
"""

import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
import torch.optim as optim

from tools.ml.motion_loader import load_motion_directory, generate_standing_motion

from .config import CALMConfig
from .networks import MotionEncoder
from .motion_buffer import MotionTransitionBuffer
from .observation import CALMObservationExtractor
from .export import export_encoder


def _select_device(requested: str) -> torch.device:
    if requested in ("cuda", "mps", "auto"):
        if torch.cuda.is_available():
            return torch.device("cuda")
        if torch.backends.mps.is_available():
            return torch.device("mps")
        return torch.device("cpu")
    return torch.device(requested)


def _extract_clip_windows(
    motions: dict,
    humanoid_config,
    num_steps: int = 10,
    dt: float = 1.0 / 60.0,
) -> list:
    """Extract sliding windows of stacked observations from each clip.

    Returns list of (clip_index, frame_offset, stacked_obs) tuples.
    """
    windows = []

    for clip_idx, (clip_name, clip) in enumerate(motions.items()):
        frames = clip["frames"]
        fps = clip["fps"]
        clip_dt = 1.0 / fps if fps > 0 else dt

        extractor = CALMObservationExtractor(humanoid_config)
        frame_obs_list = []

        for i, frame in enumerate(frames):
            root_pos = np.array(frame["root_pos"], dtype=np.float32)
            root_rot = np.array(frame["root_rot"], dtype=np.float32)
            joint_rotations = np.array(
                frame["joint_rotations"], dtype=np.float32
            )

            if "joint_positions" in frame:
                all_joint_pos = np.array(
                    frame["joint_positions"], dtype=np.float32
                )
                key_indices = [4, 13, 9, 19, 16]
                key_body_pos = np.zeros(
                    (humanoid_config.num_key_bodies, 3), dtype=np.float32
                )
                for k, idx in enumerate(key_indices):
                    if idx < len(all_joint_pos):
                        key_body_pos[k] = all_joint_pos[idx]
            else:
                key_body_pos = np.zeros(
                    (humanoid_config.num_key_bodies, 3), dtype=np.float32
                )

            prev_root_pos = (
                np.array(frames[i - 1]["root_pos"], dtype=np.float32)
                if i > 0
                else None
            )
            prev_root_rot = (
                np.array(frames[i - 1]["root_rot"], dtype=np.float32)
                if i > 0
                else None
            )

            obs = extractor.extract_frame_from_motion(
                root_pos=root_pos,
                root_rot=root_rot,
                joint_rotations=joint_rotations,
                key_body_positions=key_body_pos,
                delta_time=clip_dt,
                prev_root_pos=prev_root_pos,
                prev_root_rot=prev_root_rot,
            )
            frame_obs_list.append(obs)

        # Create sliding windows of num_steps consecutive frames
        for start in range(len(frame_obs_list) - num_steps + 1):
            stacked = np.concatenate(
                frame_obs_list[start:start + num_steps]
            )
            windows.append((clip_idx, start, stacked))

    return windows


def info_nce_loss(
    anchors: torch.Tensor,
    positives: torch.Tensor,
    negatives: torch.Tensor,
    temperature: float = 0.1,
) -> torch.Tensor:
    """InfoNCE contrastive loss.

    Args:
        anchors: (batch, latent_dim) anchor embeddings
        positives: (batch, latent_dim) positive embeddings (same clip)
        negatives: (batch, num_neg, latent_dim) negative embeddings

    Returns:
        scalar loss
    """
    # Positive similarity
    pos_sim = (anchors * positives).sum(dim=-1) / temperature  # (batch,)

    # Negative similarities
    neg_sim = torch.bmm(
        negatives, anchors.unsqueeze(-1)
    ).squeeze(-1) / temperature  # (batch, num_neg)

    # Log-sum-exp over positives and negatives
    logits = torch.cat([pos_sim.unsqueeze(-1), neg_sim], dim=-1)  # (batch, 1+num_neg)
    labels = torch.zeros(anchors.shape[0], dtype=torch.long, device=anchors.device)
    return F.cross_entropy(logits, labels)


class EncoderTrainer:
    """Phase 2: trains motion encoder with contrastive learning."""

    def __init__(self, config: CALMConfig):
        self.config = config
        self.device = _select_device(config.device)

        humanoid = config.humanoid
        enc_config = config.encoder
        train_config = config.encoder_training

        # Build encoder
        self.encoder = MotionEncoder(enc_config, humanoid).to(self.device)
        self.optimizer = optim.Adam(
            self.encoder.parameters(),
            lr=train_config.learning_rate,
        )

        # Load motion data and extract windows
        print(f"Loading motion data from {config.motion_dir}...")
        motion_data = load_motion_directory(config.motion_dir, 20)
        if not motion_data:
            print("No motion data found. Using standing motion.")
            motion_data = {"standing": generate_standing_motion(20)}
        print(f"Loaded {len(motion_data)} motion clips")

        print("Extracting observation windows...")
        self.windows = _extract_clip_windows(
            motion_data, humanoid,
            num_steps=humanoid.num_encoder_obs_steps,
        )
        print(f"Extracted {len(self.windows)} windows")

        # Group windows by clip index for positive pair sampling
        self.clip_windows = {}
        for i, (clip_idx, frame_offset, obs) in enumerate(self.windows):
            if clip_idx not in self.clip_windows:
                self.clip_windows[clip_idx] = []
            self.clip_windows[clip_idx].append((i, frame_offset, obs))

        self.clip_indices = list(self.clip_windows.keys())
        self.positive_window = train_config.positive_window
        self.negative_clips = train_config.negative_clips

        self.output_dir = Path(config.output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        print(f"Encoder: input={humanoid.encoder_obs_dim}, "
              f"output={enc_config.output_dim}")
        print(f"Device: {self.device}")

    def train(self):
        train_config = self.config.encoder_training
        num_iters = train_config.num_iterations
        batch_size = train_config.batch_size
        print(f"\nStarting encoder training for {num_iters} iterations...")

        for iteration in range(num_iters):
            iter_start = time.time()

            anchors, positives, negatives = self._sample_batch(batch_size)
            anchors = torch.tensor(anchors, dtype=torch.float32, device=self.device)
            positives = torch.tensor(positives, dtype=torch.float32, device=self.device)
            negatives = torch.tensor(negatives, dtype=torch.float32, device=self.device)

            # Encode
            z_anchor = self.encoder(anchors)
            z_positive = self.encoder(positives)
            z_neg_flat = self.encoder(
                negatives.reshape(-1, negatives.shape[-1])
            )
            z_negative = z_neg_flat.reshape(
                batch_size, self.negative_clips, -1
            )

            loss = info_nce_loss(z_anchor, z_positive, z_negative)

            self.optimizer.zero_grad()
            loss.backward()
            self.optimizer.step()

            if iteration % train_config.log_interval == 0:
                iter_time = time.time() - iter_start
                print(
                    f"Iter {iteration:6d} | "
                    f"loss={loss.item():.4f} | "
                    f"time={iter_time:.3f}s"
                )

            if (
                iteration % train_config.checkpoint_interval == 0
                and iteration > 0
            ):
                self._save_checkpoint(iteration)
                self._export()

        self._save_checkpoint(num_iters)
        self._export()
        print("\nEncoder training complete.")

    def _sample_batch(self, batch_size):
        """Sample anchor, positive, and negative observation windows."""
        rng = np.random.RandomState()
        obs_dim = self.config.humanoid.encoder_obs_dim

        anchors = np.zeros((batch_size, obs_dim), dtype=np.float32)
        positives = np.zeros((batch_size, obs_dim), dtype=np.float32)
        negatives = np.zeros(
            (batch_size, self.negative_clips, obs_dim), dtype=np.float32
        )

        for b in range(batch_size):
            # Pick random clip and anchor window
            clip_idx = self.clip_indices[
                rng.randint(len(self.clip_indices))
            ]
            clip_wins = self.clip_windows[clip_idx]
            anchor_idx = rng.randint(len(clip_wins))
            _, anchor_offset, anchor_obs = clip_wins[anchor_idx]
            anchors[b] = anchor_obs

            # Positive: same clip, within positive_window frames
            pos_candidates = [
                (i, offset, obs)
                for i, offset, obs in clip_wins
                if abs(offset - anchor_offset) <= self.positive_window
                and offset != anchor_offset
            ]
            if pos_candidates:
                pi = rng.randint(len(pos_candidates))
                positives[b] = pos_candidates[pi][2]
            else:
                positives[b] = anchor_obs  # fallback: self

            # Negatives: different clips
            other_clips = [
                c for c in self.clip_indices if c != clip_idx
            ]
            for n in range(self.negative_clips):
                if other_clips:
                    neg_clip = other_clips[rng.randint(len(other_clips))]
                    neg_wins = self.clip_windows[neg_clip]
                    ni = rng.randint(len(neg_wins))
                    negatives[b, n] = neg_wins[ni][2]
                else:
                    # Only one clip: use random window from same clip
                    ni = rng.randint(len(clip_wins))
                    negatives[b, n] = clip_wins[ni][2]

        return anchors, positives, negatives

    def _save_checkpoint(self, iteration: int):
        path = self.output_dir / f"encoder_checkpoint_{iteration:06d}.pt"
        torch.save({
            "iteration": iteration,
            "encoder_state_dict": self.encoder.state_dict(),
            "optimizer_state_dict": self.optimizer.state_dict(),
        }, path)
        print(f"  Checkpoint saved: {path}")

    def _export(self):
        export_encoder(self.encoder, str(self.output_dir))
        print(f"  Exported encoder to {self.output_dir}")


def main():
    parser = argparse.ArgumentParser(
        description="CALM Phase 2: Encoder training (contrastive)"
    )
    parser.add_argument("--llc-checkpoint", type=str, default=None,
                        help="Path to LLC checkpoint (Phase 1)")
    parser.add_argument("--config", type=str, default=None)
    parser.add_argument("--output", type=str, default="checkpoints/calm")
    parser.add_argument("--motions", type=str, default="data/calm/motions")
    parser.add_argument("--iterations", type=int, default=None)
    parser.add_argument("--device", type=str, default=None)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    config = CALMConfig()
    config.output_dir = args.output
    config.motion_dir = args.motions
    config.seed = args.seed

    if args.config:
        with open(args.config) as f:
            overrides = json.load(f)
        for key, value in overrides.items():
            if hasattr(config, key):
                setattr(config, key, value)

    if args.iterations is not None:
        config.encoder_training.num_iterations = args.iterations
    if args.device is not None:
        config.device = args.device

    np.random.seed(config.seed)
    torch.manual_seed(config.seed)

    trainer = EncoderTrainer(config)
    trainer.train()


if __name__ == "__main__":
    main()
