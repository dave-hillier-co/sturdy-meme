#!/usr/bin/env python3
"""CALM Phase 1: LLC (Low-Level Controller) training.

Trains the style-conditioned policy using PPO + AMP discriminator.
Each rollout step:
  1. Sample random latents per env (unit normal -> L2 normalize)
  2. Collect rollouts with style-conditioned policy
  3. Compute combined reward: r = w_style * r_amp + w_task * r_task
  4. PPO update with CALM-specific evaluate_actions (latent, obs, actions)
  5. Update AMP discriminator

Usage:
    python -m tools.calm_training.train_llc [--config config.json] \\
        [--output checkpoints/calm] [--motions data/calm/motions] \\
        [--iterations 5000] [--device auto] [--num-envs 16]
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

from tools.ml.config import PPOConfig
from tools.ml.policy import ValueNetwork
from tools.ml.ppo import RolloutBuffer
from tools.ml.motion_loader import load_motion_directory, generate_standing_motion

from .config import CALMConfig, LLCPolicyConfig, ValueConfig
from .networks import StyleConditionedPolicy
from .discriminator import AMPTrainer
from .motion_buffer import MotionTransitionBuffer
from .export import export_llc
from .reward import combine_rewards


class CALMRolloutBuffer:
    """Extends RolloutBuffer to also store latent codes per step."""

    def __init__(
        self,
        num_envs: int,
        horizon: int,
        obs_dim: int,
        act_dim: int,
        latent_dim: int,
        device: str = "cpu",
    ):
        self.inner = RolloutBuffer(num_envs, horizon, obs_dim, act_dim, device)
        self.latents = torch.zeros(
            horizon, num_envs, latent_dim, device=device
        )
        # Per-frame obs for AMP (single frame, not stacked)
        self.per_frame_obs_t = torch.zeros(
            horizon, num_envs, 102, device=device
        )
        self.per_frame_obs_t1 = torch.zeros(
            horizon, num_envs, 102, device=device
        )

    def add(self, obs, action, log_prob, reward, done, value, latent,
            obs_t=None, obs_t1=None):
        step = self.inner.step
        self.latents[step] = latent
        if obs_t is not None:
            self.per_frame_obs_t[step] = obs_t
        if obs_t1 is not None:
            self.per_frame_obs_t1[step] = obs_t1
        self.inner.add(obs, action, log_prob, reward, done, value)

    def compute_gae(self, last_value, gamma, gae_lambda):
        self.inner.compute_gae(last_value, gamma, gae_lambda)

    def flatten(self):
        data = self.inner.flatten()
        batch_size = self.inner.horizon * self.inner.num_envs
        data["latents"] = self.latents.reshape(batch_size, -1)
        data["per_frame_obs_t"] = self.per_frame_obs_t.reshape(batch_size, -1)
        data["per_frame_obs_t1"] = self.per_frame_obs_t1.reshape(batch_size, -1)
        return data

    def reset(self):
        self.inner.reset()


def calm_ppo_update(
    policy: StyleConditionedPolicy,
    value_net: ValueNetwork,
    optimizer: optim.Optimizer,
    buffer: CALMRolloutBuffer,
    config: PPOConfig,
) -> dict:
    """PPO update for style-conditioned policy.

    Same clipped surrogate logic as tools.ml.ppo.ppo_update but passes
    latents to the policy's evaluate_actions method.
    """
    data = buffer.flatten()

    adv = data["advantages"]
    adv = (adv - adv.mean()) / (adv.std() + 1e-8)

    total_policy_loss = 0.0
    total_value_loss = 0.0
    total_entropy = 0.0
    total_approx_kl = 0.0
    num_updates = 0

    batch_size = adv.shape[0]
    device = adv.device

    for epoch in range(config.num_epochs):
        indices = torch.randperm(batch_size, device=device)

        for start in range(0, batch_size, config.minibatch_size):
            end = start + config.minibatch_size
            mb_idx = indices[start:end]

            mb_obs = data["observations"][mb_idx]
            mb_actions = data["actions"][mb_idx]
            mb_latents = data["latents"][mb_idx]
            mb_old_log_probs = data["log_probs"][mb_idx]
            mb_advantages = adv[mb_idx]
            mb_returns = data["returns"][mb_idx]

            new_log_probs, entropy = policy.evaluate_actions(
                mb_latents, mb_obs, mb_actions
            )
            new_values = value_net(mb_obs)

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
                list(policy.parameters()) + list(value_net.parameters()),
                config.max_grad_norm,
            )
            optimizer.step()

            with torch.no_grad():
                approx_kl = (mb_old_log_probs - new_log_probs).mean()
                total_policy_loss += policy_loss.item()
                total_value_loss += value_loss.item()
                total_entropy += entropy.mean().item()
                total_approx_kl += approx_kl.item()
                num_updates += 1

    n = max(num_updates, 1)
    return {
        "policy_loss": total_policy_loss / n,
        "value_loss": total_value_loss / n,
        "entropy": total_entropy / n,
        "approx_kl": total_approx_kl / n,
    }


def _select_device(requested: str) -> torch.device:
    """Select best available device."""
    if requested == "cuda" and torch.cuda.is_available():
        return torch.device("cuda")
    if requested == "mps" and torch.backends.mps.is_available():
        return torch.device("mps")
    if requested in ("cuda", "mps", "auto"):
        if torch.cuda.is_available():
            return torch.device("cuda")
        if torch.backends.mps.is_available():
            return torch.device("mps")
        return torch.device("cpu")
    return torch.device(requested)


class LLCTrainer:
    """Phase 1: trains LLC with PPO + AMP."""

    def __init__(self, config: CALMConfig):
        self.config = config
        self.device = _select_device(config.device)
        self.vec_env = None

        actual_num_envs = config.env.num_envs
        if config.max_envs > 0:
            actual_num_envs = min(actual_num_envs, config.max_envs)
        if config.ppo.num_envs != actual_num_envs:
            config.ppo.num_envs = actual_num_envs

        # Load motion data
        print(f"Loading motion data from {config.motion_dir}...")
        self.motion_data = load_motion_directory(
            config.motion_dir, 20  # 20 joints for motion loader
        )
        if not self.motion_data:
            print("No motion data found. Using standing motion for bootstrap.")
            self.motion_data = {"standing": generate_standing_motion(20)}
        print(f"Loaded {len(self.motion_data)} motion clips")

        # Pre-extract motion transitions for AMP
        self.motion_buffer = MotionTransitionBuffer(config.humanoid)
        n_trans = self.motion_buffer.extract_from_motions(
            self.motion_data, config.env.sim_timestep
        )
        print(f"Extracted {n_trans} motion transitions for AMP")

        # Create environments
        if config.parallel:
            from .vec_env import CALMSubprocVecEnv
            num_workers = config.num_workers
            if num_workers <= 0:
                num_workers = max(1, (os.cpu_count() or 2) - 1)
            print(
                f"Creating {actual_num_envs} environments "
                f"across {num_workers} workers..."
            )
            self.vec_env = CALMSubprocVecEnv(
                config, actual_num_envs, num_workers,
                config.motion_dir, config.seed,
            )
            self.num_envs = actual_num_envs
        else:
            from .environment import CALMEnv
            print(f"Creating {actual_num_envs} environments...")
            self.envs = [
                CALMEnv(config, motion_data=self.motion_data)
                for _ in range(actual_num_envs)
            ]
            self.num_envs = actual_num_envs

        humanoid = config.humanoid
        obs_dim = humanoid.policy_obs_dim
        act_dim = humanoid.num_dof

        # Networks
        self.policy = StyleConditionedPolicy(
            config.llc_policy, humanoid
        ).to(self.device)
        value_config = config.value
        from tools.ml.config import PolicyConfig
        vnet_config = PolicyConfig(
            hidden_layers=len(value_config.hidden_sizes),
            hidden_dim=value_config.hidden_sizes[0],
            activation=value_config.activation,
        )
        self.value_net = ValueNetwork(obs_dim, vnet_config).to(self.device)
        self.optimizer = optim.Adam(
            list(self.policy.parameters()) + list(self.value_net.parameters()),
            lr=config.ppo.learning_rate,
        )

        # AMP discriminator
        self.amp_trainer = AMPTrainer(config.amp, humanoid, str(self.device))

        # Rollout buffer
        self.buffer = CALMRolloutBuffer(
            self.num_envs,
            config.ppo.samples_per_env,
            obs_dim,
            act_dim,
            humanoid.latent_dim,
            str(self.device),
        )

        self.iteration = 0
        self.total_timesteps = 0
        self.best_reward = -float("inf")
        self.output_dir = Path(config.output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        print(f"LLC Policy: latent_dim={humanoid.latent_dim}, "
              f"obs_dim={obs_dim}, act_dim={act_dim}")
        print(f"Device: {self.device}")

    def train(self):
        """Run the LLC training loop."""
        ppo = self.config.ppo
        humanoid = self.config.humanoid
        print(f"\nStarting LLC training for {ppo.num_iterations} iterations...")

        # Reset environments
        if self.vec_env is not None:
            obs_np = self.vec_env.reset()
        else:
            obs_list = [
                env.reset(seed=self.config.seed + i)
                for i, env in enumerate(self.envs)
            ]
            obs_np = np.stack(obs_list)
        obs = torch.tensor(obs_np, dtype=torch.float32, device=self.device)

        for iteration in range(ppo.num_iterations):
            self.iteration = iteration
            iter_start = time.time()

            # Sample latents for this rollout (one per env, held fixed)
            latents = torch.randn(
                self.num_envs, humanoid.latent_dim, device=self.device
            )
            latents = torch.nn.functional.normalize(latents, dim=-1)

            # Collect rollouts
            self.buffer.reset()
            episode_rewards = []
            all_obs_t = []
            all_obs_t1 = []

            with torch.no_grad():
                for step in range(ppo.samples_per_env):
                    dist = self.policy.get_distribution(latents, obs)
                    action = dist.sample()
                    log_prob = dist.log_prob(action).sum(dim=-1)
                    value = self.value_net(obs)

                    action_np = action.cpu().numpy()

                    if self.vec_env is not None:
                        new_obs_np, rewards_np, dones_np, infos = (
                            self.vec_env.step(action_np)
                        )
                    else:
                        new_obs_list = []
                        rewards_list = []
                        dones_list = []
                        infos = []
                        for i, env in enumerate(self.envs):
                            ob, reward, done, info = env.step(action_np[i])
                            rewards_list.append(reward)
                            dones_list.append(float(done))
                            if done:
                                episode_rewards.append(reward)
                                ob = env.reset()
                            new_obs_list.append(ob)
                            infos.append(info)
                        new_obs_np = np.stack(new_obs_list)
                        rewards_np = np.array(rewards_list, dtype=np.float32)
                        dones_np = np.array(dones_list, dtype=np.float32)

                    # Collect AMP transition pairs
                    obs_t_batch = np.stack(
                        [info["obs_t"] for info in infos]
                    )
                    obs_t1_batch = np.stack(
                        [info["obs_t1"] for info in infos]
                    )
                    obs_t_tensor = torch.tensor(
                        obs_t_batch, dtype=torch.float32, device=self.device
                    )
                    obs_t1_tensor = torch.tensor(
                        obs_t1_batch, dtype=torch.float32, device=self.device
                    )

                    # Compute AMP style reward
                    style_reward = self.amp_trainer.compute_style_reward(
                        obs_t_tensor, obs_t1_tensor
                    )
                    task_reward = torch.tensor(
                        rewards_np, dtype=torch.float32, device=self.device
                    )
                    combined_reward = combine_rewards(
                        style_reward,
                        task_reward,
                        self.config.amp.style_reward_weight,
                        self.config.amp.task_reward_weight,
                    )

                    new_obs = torch.tensor(
                        new_obs_np, dtype=torch.float32, device=self.device
                    )
                    done_t = torch.tensor(
                        dones_np, dtype=torch.float32, device=self.device
                    )

                    self.buffer.add(
                        obs, action, log_prob, combined_reward, done_t,
                        value, latents, obs_t_tensor, obs_t1_tensor,
                    )
                    all_obs_t.append(obs_t_tensor)
                    all_obs_t1.append(obs_t1_tensor)
                    obs = new_obs

                last_value = self.value_net(obs)
                self.buffer.compute_gae(
                    last_value, ppo.gamma, ppo.gae_lambda
                )

            # PPO update
            ppo_metrics = calm_ppo_update(
                self.policy, self.value_net, self.optimizer,
                self.buffer, ppo,
            )

            # AMP discriminator update
            amp_batch_size = min(256, len(self.motion_buffer))
            if amp_batch_size > 0:
                real_t_np, real_t1_np = self.motion_buffer.sample(
                    amp_batch_size
                )
                real_t = torch.tensor(
                    real_t_np, dtype=torch.float32
                )
                real_t1 = torch.tensor(
                    real_t1_np, dtype=torch.float32
                )

                # Use collected fake transitions
                fake_obs_t = torch.cat(all_obs_t, dim=0)
                fake_obs_t1 = torch.cat(all_obs_t1, dim=0)
                fake_idx = torch.randint(
                    0, fake_obs_t.shape[0], (amp_batch_size,)
                )
                fake_t = fake_obs_t[fake_idx].cpu()
                fake_t1 = fake_obs_t1[fake_idx].cpu()

                disc_metrics = self.amp_trainer.update(
                    real_t, real_t1, fake_t, fake_t1
                )
            else:
                disc_metrics = {
                    "disc_loss": 0.0,
                    "real_score": 0.0,
                    "fake_score": 0.0,
                    "grad_penalty": 0.0,
                }

            self.total_timesteps += self.num_envs * ppo.samples_per_env
            iter_time = time.time() - iter_start

            if iteration % self.config.log_interval == 0:
                mean_reward = (
                    np.mean(episode_rewards) if episode_rewards else 0.0
                )
                print(
                    f"Iter {iteration:6d} | "
                    f"reward={mean_reward:.4f} | "
                    f"p_loss={ppo_metrics['policy_loss']:.4f} | "
                    f"v_loss={ppo_metrics['value_loss']:.4f} | "
                    f"entropy={ppo_metrics['entropy']:.4f} | "
                    f"d_loss={disc_metrics['disc_loss']:.4f} | "
                    f"real={disc_metrics['real_score']:.3f} | "
                    f"fake={disc_metrics['fake_score']:.3f} | "
                    f"time={iter_time:.2f}s"
                )

            if (
                iteration % self.config.checkpoint_interval == 0
                and iteration > 0
            ):
                self._save_checkpoint(iteration)
                self._export_best()

        self._save_checkpoint(ppo.num_iterations)
        self._export_best()
        print(f"\nLLC training complete. Total timesteps: {self.total_timesteps}")

    def _save_checkpoint(self, iteration: int):
        path = self.output_dir / f"llc_checkpoint_{iteration:06d}.pt"
        torch.save({
            "iteration": iteration,
            "policy_state_dict": self.policy.state_dict(),
            "value_state_dict": self.value_net.state_dict(),
            "optimizer_state_dict": self.optimizer.state_dict(),
            "discriminator_state_dict":
                self.amp_trainer.discriminator.state_dict(),
            "total_timesteps": self.total_timesteps,
        }, path)
        print(f"  Checkpoint saved: {path}")

    def _export_best(self):
        export_llc(self.policy, str(self.output_dir))
        print(f"  Exported LLC to {self.output_dir}")

    def close(self):
        if self.vec_env is not None:
            self.vec_env.close()
            self.vec_env = None


def main():
    parser = argparse.ArgumentParser(
        description="CALM Phase 1: LLC training (PPO + AMP)"
    )
    parser.add_argument("--config", type=str, default=None,
                        help="Path to JSON config file")
    parser.add_argument("--output", type=str, default="checkpoints/calm")
    parser.add_argument("--motions", type=str, default="data/calm/motions")
    parser.add_argument("--iterations", type=int, default=None)
    parser.add_argument("--num-envs", type=int, default=None)
    parser.add_argument("--device", type=str, default=None)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--parallel", action="store_true")
    parser.add_argument("--num-workers", type=int, default=None)
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
    if args.parallel:
        config.parallel = True
    if args.num_workers is not None:
        config.num_workers = args.num_workers

    np.random.seed(config.seed)
    torch.manual_seed(config.seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed(config.seed)
    if torch.backends.mps.is_available():
        torch.mps.manual_seed(config.seed)

    trainer = LLCTrainer(config)
    try:
        trainer.train()
    finally:
        trainer.close()


if __name__ == "__main__":
    main()
