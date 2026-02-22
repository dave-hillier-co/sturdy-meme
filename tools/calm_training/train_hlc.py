#!/usr/bin/env python3
"""CALM Phase 3: HLC (High-Level Controller) training.

Trains task-specific HLCs using PPO with a frozen LLC.
The HLC outputs latent commands -> LLC produces joint actions.

Supported tasks:
  - heading: match target direction and speed
  - location: reach target position
  - strike: move hand to strike target

Usage:
    python -m tools.calm_training.train_hlc \\
        --llc-checkpoint checkpoints/calm/llc_checkpoint_005000.pt \\
        --task heading \\
        [--iterations 2000] [--device auto] [--num-envs 16]
"""

import argparse
import json
import os
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim

from tools.ml.config import PolicyConfig, PPOConfig
from tools.ml.policy import ValueNetwork
from tools.ml.ppo import RolloutBuffer, ppo_update
from tools.ml.motion_loader import load_motion_directory, generate_standing_motion

from .config import CALMConfig, HLCTaskConfig
from .networks import StyleConditionedPolicy, HLCPolicy
from .environment import CALMEnv
from .export import export_hlc
from .observation import get_heading_angle


def _select_device(requested: str) -> torch.device:
    if requested in ("cuda", "mps", "auto"):
        if torch.cuda.is_available():
            return torch.device("cuda")
        if torch.backends.mps.is_available():
            return torch.device("mps")
        return torch.device("cpu")
    return torch.device(requested)


class HLCRolloutBuffer:
    """Rollout buffer for HLC that stores task observations."""

    def __init__(
        self,
        num_envs: int,
        horizon: int,
        task_obs_dim: int,
        latent_dim: int,
        device: str = "cpu",
    ):
        self.num_envs = num_envs
        self.horizon = horizon
        self.device = device

        self.task_obs = torch.zeros(
            horizon, num_envs, task_obs_dim, device=device
        )
        self.latents = torch.zeros(
            horizon, num_envs, latent_dim, device=device
        )
        self.log_probs = torch.zeros(horizon, num_envs, device=device)
        self.rewards = torch.zeros(horizon, num_envs, device=device)
        self.dones = torch.zeros(horizon, num_envs, device=device)
        self.values = torch.zeros(horizon, num_envs, device=device)
        self.advantages = torch.zeros(horizon, num_envs, device=device)
        self.returns = torch.zeros(horizon, num_envs, device=device)
        self.step = 0

    def add(self, task_obs, latent, log_prob, reward, done, value):
        self.task_obs[self.step] = task_obs
        self.latents[self.step] = latent
        self.log_probs[self.step] = log_prob
        self.rewards[self.step] = reward
        self.dones[self.step] = done
        self.values[self.step] = value
        self.step += 1

    def compute_gae(self, last_value, gamma, gae_lambda):
        last_gae = 0
        for t in reversed(range(self.horizon)):
            if t == self.horizon - 1:
                next_value = last_value
            else:
                next_value = self.values[t + 1]
            next_non_terminal = 1.0 - self.dones[t]
            delta = (
                self.rewards[t]
                + gamma * next_value * next_non_terminal
                - self.values[t]
            )
            self.advantages[t] = last_gae = (
                delta + gamma * gae_lambda * next_non_terminal * last_gae
            )
        self.returns = self.advantages + self.values

    def flatten(self):
        batch_size = self.horizon * self.num_envs
        return {
            "task_obs": self.task_obs.reshape(batch_size, -1),
            "latents": self.latents.reshape(batch_size, -1),
            "log_probs": self.log_probs.reshape(batch_size),
            "advantages": self.advantages.reshape(batch_size),
            "returns": self.returns.reshape(batch_size),
        }

    def reset(self):
        self.step = 0


def hlc_ppo_update(
    hlc: HLCPolicy,
    value_net: ValueNetwork,
    optimizer: optim.Optimizer,
    buffer: HLCRolloutBuffer,
    config: PPOConfig,
) -> dict:
    """PPO update for HLC."""
    data = buffer.flatten()
    adv = data["advantages"]
    adv = (adv - adv.mean()) / (adv.std() + 1e-8)

    total_policy_loss = 0.0
    total_value_loss = 0.0
    total_entropy = 0.0
    num_updates = 0

    batch_size = adv.shape[0]
    device = adv.device

    for epoch in range(config.num_epochs):
        indices = torch.randperm(batch_size, device=device)
        for start in range(0, batch_size, config.minibatch_size):
            end = start + config.minibatch_size
            mb_idx = indices[start:end]

            mb_task_obs = data["task_obs"][mb_idx]
            mb_latents = data["latents"][mb_idx]
            mb_old_log_probs = data["log_probs"][mb_idx]
            mb_advantages = adv[mb_idx]
            mb_returns = data["returns"][mb_idx]

            new_log_probs, entropy = hlc.evaluate_actions(
                mb_task_obs, mb_latents
            )
            new_values = value_net(mb_task_obs)

            ratio = (new_log_probs - mb_old_log_probs).exp()
            surr1 = ratio * mb_advantages
            surr2 = torch.clamp(
                ratio, 1 - config.clip_epsilon, 1 + config.clip_epsilon
            ) * mb_advantages
            policy_loss = -torch.min(surr1, surr2).mean()

            value_loss = nn.functional.mse_loss(new_values, mb_returns)
            entropy_loss = -entropy.mean()

            loss = (
                policy_loss
                + config.value_loss_coeff * value_loss
                + config.entropy_coeff * entropy_loss
            )

            optimizer.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(
                list(hlc.parameters()) + list(value_net.parameters()),
                config.max_grad_norm,
            )
            optimizer.step()

            with torch.no_grad():
                total_policy_loss += policy_loss.item()
                total_value_loss += value_loss.item()
                total_entropy += entropy.mean().item()
                num_updates += 1

    n = max(num_updates, 1)
    return {
        "policy_loss": total_policy_loss / n,
        "value_loss": total_value_loss / n,
        "entropy": total_entropy / n,
    }


class TaskGenerator:
    """Generates task-specific observations and rewards."""

    def __init__(self, task: str, num_envs: int, device: str = "cpu"):
        self.task = task
        self.num_envs = num_envs
        self.device = device

        # Task targets (refreshed each episode)
        if task == "heading":
            self.task_obs_dim = 2
            self.target_headings = torch.zeros(num_envs, device=device)
        elif task == "location":
            self.task_obs_dim = 3
            self.target_positions = torch.zeros(num_envs, 3, device=device)
        elif task == "strike":
            self.task_obs_dim = 6
            self.target_positions = torch.zeros(num_envs, 3, device=device)
        else:
            raise ValueError(f"Unknown task: {task}")

    def reset_targets(self, env_mask: torch.Tensor = None):
        """Randomize targets for environments that need reset."""
        if env_mask is None:
            env_mask = torch.ones(self.num_envs, dtype=torch.bool)
        n = env_mask.sum().item()
        if n == 0:
            return

        if self.task == "heading":
            self.target_headings[env_mask] = (
                torch.rand(n, device=self.device) * 2 * np.pi - np.pi
            )
        elif self.task == "location":
            self.target_positions[env_mask] = (
                torch.rand(n, 3, device=self.device) * 6 - 3
            )
            self.target_positions[env_mask, 1] = 0.0  # Y=0 (ground)
        elif self.task == "strike":
            self.target_positions[env_mask] = (
                torch.rand(n, 3, device=self.device) * 2 - 1
            )
            self.target_positions[env_mask, 1] += 1.0  # above ground

    def get_task_obs(self, infos: list) -> torch.Tensor:
        """Compute task observations from environment infos."""
        batch_size = len(infos)
        task_obs = torch.zeros(
            batch_size, self.task_obs_dim, device=self.device
        )

        if self.task == "heading":
            for i in range(batch_size):
                # Task obs = (sin(target), cos(target))
                task_obs[i, 0] = torch.sin(self.target_headings[i])
                task_obs[i, 1] = torch.cos(self.target_headings[i])

        elif self.task == "location":
            for i in range(batch_size):
                root_pos = infos[i].get("root_position", np.zeros(3))
                target = self.target_positions[i].cpu().numpy()
                rel = target - np.array(root_pos, dtype=np.float32)
                task_obs[i] = torch.tensor(rel, device=self.device)

        elif self.task == "strike":
            for i in range(batch_size):
                target = self.target_positions[i]
                hand_pos = torch.tensor(
                    infos[i].get("hand_position", np.zeros(3)),
                    dtype=torch.float32,
                    device=self.device,
                )
                task_obs[i, :3] = target
                task_obs[i, 3:6] = hand_pos

        return task_obs

    def compute_reward(self, infos: list) -> torch.Tensor:
        """Compute task-specific reward."""
        batch_size = len(infos)
        rewards = torch.zeros(batch_size, device=self.device)

        if self.task == "heading":
            for i in range(batch_size):
                # Reward for facing target direction
                current = infos[i].get("heading", 0.0)
                target = self.target_headings[i].item()
                diff = abs(current - target)
                diff = min(diff, 2 * np.pi - diff)
                rewards[i] = float(np.exp(-2.0 * diff))

        elif self.task == "location":
            for i in range(batch_size):
                root_pos = np.array(
                    infos[i].get("root_position", np.zeros(3)),
                    dtype=np.float32,
                )
                target = self.target_positions[i].cpu().numpy()
                dist = float(np.linalg.norm(root_pos - target))
                rewards[i] = float(np.exp(-1.0 * dist))

        elif self.task == "strike":
            for i in range(batch_size):
                hand_pos = np.array(
                    infos[i].get("hand_position", np.zeros(3)),
                    dtype=np.float32,
                )
                target = self.target_positions[i].cpu().numpy()
                dist = float(np.linalg.norm(hand_pos - target))
                rewards[i] = float(np.exp(-5.0 * dist))

        return rewards


class HLCTrainer:
    """Phase 3: trains task HLC with PPO using frozen LLC."""

    def __init__(self, config: CALMConfig, task: str):
        self.config = config
        self.task = task
        self.device = _select_device(config.device)

        humanoid = config.humanoid

        # Get task config
        task_configs = {
            "heading": config.hlc.heading,
            "location": config.hlc.location,
            "strike": config.hlc.strike,
        }
        if task not in task_configs:
            raise ValueError(f"Unknown task: {task}")
        self.task_config = task_configs[task]

        actual_num_envs = config.env.num_envs
        if config.max_envs > 0:
            actual_num_envs = min(actual_num_envs, config.max_envs)

        # Load motion data for environments
        motion_data = load_motion_directory(config.motion_dir, 20)
        if not motion_data:
            motion_data = {"standing": generate_standing_motion(20)}

        # Create environments
        print(f"Creating {actual_num_envs} environments...")
        self.envs = [
            CALMEnv(config, motion_data=motion_data)
            for _ in range(actual_num_envs)
        ]
        self.num_envs = actual_num_envs

        # Frozen LLC (loaded from checkpoint or initialized)
        self.llc = StyleConditionedPolicy(
            config.llc_policy, humanoid
        ).to(self.device)
        self.llc.eval()
        for p in self.llc.parameters():
            p.requires_grad = False

        # HLC
        self.hlc = HLCPolicy(self.task_config, humanoid).to(self.device)
        value_config = PolicyConfig(
            hidden_layers=len(self.task_config.hidden_sizes),
            hidden_dim=self.task_config.hidden_sizes[0],
            activation="relu",
        )
        self.value_net = ValueNetwork(
            self.task_config.task_obs_dim, value_config
        ).to(self.device)

        self.optimizer = optim.Adam(
            list(self.hlc.parameters()) + list(self.value_net.parameters()),
            lr=self.task_config.learning_rate,
        )

        # Task generator
        self.task_gen = TaskGenerator(task, self.num_envs, str(self.device))

        # Rollout buffer
        ppo = config.ppo
        self.buffer = HLCRolloutBuffer(
            self.num_envs,
            ppo.samples_per_env,
            self.task_config.task_obs_dim,
            humanoid.latent_dim,
            str(self.device),
        )

        self.iteration = 0
        self.total_timesteps = 0
        self.output_dir = Path(config.output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        print(f"HLC ({task}): task_obs_dim={self.task_config.task_obs_dim}, "
              f"latent_dim={humanoid.latent_dim}")
        print(f"Device: {self.device}")

    def load_llc_checkpoint(self, path: str):
        """Load LLC weights from a Phase 1 checkpoint."""
        checkpoint = torch.load(path, map_location=self.device, weights_only=True)
        self.llc.load_state_dict(checkpoint["policy_state_dict"])
        print(f"Loaded LLC from {path}")

    def train(self):
        ppo = self.config.ppo
        print(f"\nStarting HLC ({self.task}) training for "
              f"{ppo.num_iterations} iterations...")

        # Reset
        obs_list = [
            env.reset(seed=self.config.seed + i)
            for i, env in enumerate(self.envs)
        ]
        obs_np = np.stack(obs_list)
        obs = torch.tensor(obs_np, dtype=torch.float32, device=self.device)

        self.task_gen.reset_targets()

        for iteration in range(ppo.num_iterations):
            self.iteration = iteration
            iter_start = time.time()

            self.buffer.reset()
            episode_rewards = []

            with torch.no_grad():
                for step in range(ppo.samples_per_env):
                    # Get task observations from current state
                    dummy_infos = [
                        {"root_position": np.zeros(3), "heading": 0.0,
                         "hand_position": np.zeros(3)}
                        for _ in range(self.num_envs)
                    ]
                    task_obs = self.task_gen.get_task_obs(dummy_infos)

                    # HLC produces latent command
                    dist = self.hlc.get_distribution(task_obs)
                    latent = dist.sample()
                    log_prob = dist.log_prob(latent).sum(dim=-1)

                    # Normalize latent for LLC
                    latent_norm = torch.nn.functional.normalize(latent, dim=-1)

                    # LLC produces actions from latent + policy obs
                    action = self.llc(latent_norm, obs)
                    action_np = action.cpu().numpy()

                    # Step environments
                    new_obs_list = []
                    rewards_list = []
                    dones_list = []
                    infos = []
                    for i, env in enumerate(self.envs):
                        ob, _, done, info = env.step(action_np[i])
                        if done:
                            ob = env.reset()
                        new_obs_list.append(ob)
                        dones_list.append(float(done))
                        infos.append(info)

                    # Compute task reward
                    task_reward = self.task_gen.compute_reward(infos)
                    for i, done in enumerate(dones_list):
                        rewards_list.append(task_reward[i].item())
                        if done:
                            episode_rewards.append(task_reward[i].item())

                    new_obs = torch.tensor(
                        np.stack(new_obs_list),
                        dtype=torch.float32, device=self.device,
                    )
                    done_t = torch.tensor(
                        dones_list, dtype=torch.float32, device=self.device
                    )
                    reward_t = torch.tensor(
                        rewards_list, dtype=torch.float32, device=self.device
                    )

                    value = self.value_net(task_obs)
                    self.buffer.add(
                        task_obs, latent, log_prob, reward_t, done_t, value
                    )

                    # Reset targets for done environments
                    done_mask = done_t.bool()
                    if done_mask.any():
                        self.task_gen.reset_targets(done_mask)

                    obs = new_obs

                # Compute GAE
                final_task_obs = self.task_gen.get_task_obs(dummy_infos)
                last_value = self.value_net(final_task_obs)
                self.buffer.compute_gae(
                    last_value, ppo.gamma, ppo.gae_lambda
                )

            # PPO update
            metrics = hlc_ppo_update(
                self.hlc, self.value_net, self.optimizer,
                self.buffer, ppo,
            )

            self.total_timesteps += self.num_envs * ppo.samples_per_env
            iter_time = time.time() - iter_start

            if iteration % self.config.log_interval == 0:
                mean_reward = (
                    np.mean(episode_rewards) if episode_rewards else 0.0
                )
                print(
                    f"Iter {iteration:6d} | "
                    f"reward={mean_reward:.4f} | "
                    f"p_loss={metrics['policy_loss']:.4f} | "
                    f"v_loss={metrics['value_loss']:.4f} | "
                    f"entropy={metrics['entropy']:.4f} | "
                    f"time={iter_time:.2f}s"
                )

            if (
                iteration % self.config.checkpoint_interval == 0
                and iteration > 0
            ):
                self._save_checkpoint(iteration)
                self._export()

        self._save_checkpoint(ppo.num_iterations)
        self._export()
        print(f"\nHLC ({self.task}) training complete.")

    def _save_checkpoint(self, iteration: int):
        path = (
            self.output_dir
            / f"hlc_{self.task}_checkpoint_{iteration:06d}.pt"
        )
        torch.save({
            "iteration": iteration,
            "hlc_state_dict": self.hlc.state_dict(),
            "value_state_dict": self.value_net.state_dict(),
            "optimizer_state_dict": self.optimizer.state_dict(),
            "total_timesteps": self.total_timesteps,
        }, path)
        print(f"  Checkpoint saved: {path}")

    def _export(self):
        export_hlc(self.hlc, self.task, str(self.output_dir))
        print(f"  Exported hlc_{self.task}.bin to {self.output_dir}")


def main():
    parser = argparse.ArgumentParser(
        description="CALM Phase 3: HLC training (task PPO)"
    )
    parser.add_argument("--llc-checkpoint", type=str, default=None,
                        help="Path to LLC checkpoint (Phase 1)")
    parser.add_argument("--task", type=str, required=True,
                        choices=["heading", "location", "strike"],
                        help="Task to train")
    parser.add_argument("--config", type=str, default=None)
    parser.add_argument("--output", type=str, default="checkpoints/calm")
    parser.add_argument("--motions", type=str, default="data/calm/motions")
    parser.add_argument("--iterations", type=int, default=None)
    parser.add_argument("--num-envs", type=int, default=None)
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
        config.ppo.num_iterations = args.iterations
    if args.num_envs is not None:
        config.ppo.num_envs = args.num_envs
        config.env.num_envs = args.num_envs
        config.max_envs = args.num_envs
    if args.device is not None:
        config.device = args.device

    np.random.seed(config.seed)
    torch.manual_seed(config.seed)

    trainer = HLCTrainer(config, args.task)
    if args.llc_checkpoint:
        trainer.load_llc_checkpoint(args.llc_checkpoint)
    trainer.train()


if __name__ == "__main__":
    main()
