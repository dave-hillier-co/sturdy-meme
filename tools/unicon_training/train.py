#!/usr/bin/env python3
"""UniCon training entry point.

Wires the generic PPO trainer with the UniCon-specific environment,
reward, and state encoding.

Usage:
    python -m tools.unicon_training.train [--config config.json] [--output generated/unicon]
"""

import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
import torch.optim as optim

from tools.ml.config import PolicyConfig
from tools.ml.policy import MLPPolicy, ValueNetwork
from tools.ml.ppo import RolloutBuffer, ppo_update
from tools.ml.export import export_policy
from tools.ml.motion_loader import load_motion_directory, generate_standing_motion

from .config import TrainingConfig
from .environment import UniConEnv


class UniConTrainer:
    """Trains a UniCon policy using PPO."""

    def __init__(self, config: TrainingConfig):
        self.config = config
        self.device = torch.device(config.device if torch.cuda.is_available() else "cpu")

        # Load motion data
        print(f"Loading motion data from {config.motion_dir}...")
        self.motion_data = load_motion_directory(
            config.motion_dir, config.humanoid.num_bodies
        )
        if not self.motion_data:
            print("No motion data found. Using standing motion for bootstrap training.")
            self.motion_data = {
                "standing": generate_standing_motion(config.humanoid.num_bodies)
            }
        print(f"Loaded {len(self.motion_data)} motion clips")

        # Create environments (cap for CPU training)
        actual_num_envs = min(config.ppo.num_envs, 32)
        print(f"Creating {actual_num_envs} environments...")
        self.envs = [
            UniConEnv(config, motion_data=self.motion_data)
            for _ in range(actual_num_envs)
        ]
        self.num_envs = actual_num_envs

        obs_dim = self.envs[0].obs_dim
        act_dim = self.envs[0].act_dim

        # Networks
        self.policy = MLPPolicy(obs_dim, act_dim, config.policy).to(self.device)
        self.value_net = ValueNetwork(obs_dim, config.policy).to(self.device)
        self.optimizer = optim.Adam(
            list(self.policy.parameters()) + list(self.value_net.parameters()),
            lr=config.ppo.learning_rate,
        )

        # Rollout buffer
        self.buffer = RolloutBuffer(
            self.num_envs, config.ppo.samples_per_env,
            obs_dim, act_dim, self.device,
        )

        self.iteration = 0
        self.total_timesteps = 0
        self.best_reward = -float("inf")
        self.output_dir = Path(config.output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        print(f"Policy: input_dim={obs_dim}, output_dim={act_dim}")
        print(f"Device: {self.device}")

    def train(self):
        """Run the training loop."""
        ppo = self.config.ppo
        print(f"\nStarting training for {ppo.num_iterations} iterations...")

        obs_list = [
            env.reset(seed=self.config.seed + i)
            for i, env in enumerate(self.envs)
        ]
        obs = torch.tensor(np.stack(obs_list), dtype=torch.float32, device=self.device)

        for iteration in range(ppo.num_iterations):
            self.iteration = iteration
            iter_start = time.time()

            self._anneal_log_std()

            # Collect rollouts
            self.buffer.reset()
            episode_rewards = []
            episode_lengths = []

            with torch.no_grad():
                for step in range(ppo.samples_per_env):
                    dist = self.policy.get_distribution(obs)
                    action = dist.sample()
                    log_prob = dist.log_prob(action).sum(dim=-1)
                    value = self.value_net(obs)

                    action_np = action.cpu().numpy()
                    new_obs_list = []
                    rewards = []
                    dones = []

                    for i, env in enumerate(self.envs):
                        ob, reward, done, info = env.step(action_np[i])
                        rewards.append(reward)
                        dones.append(float(done))

                        if done:
                            episode_rewards.append(info.get("episode_reward", reward))
                            episode_lengths.append(info.get("episode_length", 0))
                            ob = env.reset()

                        new_obs_list.append(ob)

                    new_obs = torch.tensor(
                        np.stack(new_obs_list), dtype=torch.float32, device=self.device
                    )
                    reward_t = torch.tensor(rewards, dtype=torch.float32, device=self.device)
                    done_t = torch.tensor(dones, dtype=torch.float32, device=self.device)

                    self.buffer.add(obs, action, log_prob, reward_t, done_t, value)
                    obs = new_obs

                last_value = self.value_net(obs)
                self.buffer.compute_gae(last_value, ppo.gamma, ppo.gae_lambda)

            # PPO update (generic)
            metrics = ppo_update(self.policy, self.value_net, self.optimizer,
                                 self.buffer, ppo)

            self.total_timesteps += self.num_envs * ppo.samples_per_env
            iter_time = time.time() - iter_start

            if iteration % ppo.log_interval == 0:
                mean_reward = np.mean(episode_rewards) if episode_rewards else 0.0
                mean_length = np.mean(episode_lengths) if episode_lengths else 0.0
                print(
                    f"Iter {iteration:6d} | "
                    f"reward={mean_reward:.4f} | "
                    f"ep_len={mean_length:.0f} | "
                    f"policy_loss={metrics['policy_loss']:.4f} | "
                    f"value_loss={metrics['value_loss']:.4f} | "
                    f"entropy={metrics['entropy']:.4f} | "
                    f"kl={metrics['approx_kl']:.6f} | "
                    f"timesteps={self.total_timesteps} | "
                    f"time={iter_time:.2f}s"
                )

            if iteration % ppo.checkpoint_interval == 0 and iteration > 0:
                self._save_checkpoint(iteration)
                mean_reward = np.mean(episode_rewards) if episode_rewards else 0.0
                if mean_reward > self.best_reward:
                    self.best_reward = mean_reward
                    self._export_best()

        self._save_checkpoint(ppo.num_iterations)
        self._export_best()
        print(f"\nTraining complete. Best reward: {self.best_reward:.4f}")

    def _anneal_log_std(self):
        progress = min(self.iteration / max(self.config.log_std_anneal_iterations, 1), 1.0)
        target = (self.config.initial_log_std
                  + progress * (self.config.final_log_std - self.config.initial_log_std))
        self.policy.log_std.data.fill_(target)

    def _save_checkpoint(self, iteration: int):
        path = self.output_dir / f"checkpoint_{iteration:06d}.pt"
        torch.save({
            "iteration": iteration,
            "policy_state_dict": self.policy.state_dict(),
            "value_state_dict": self.value_net.state_dict(),
            "optimizer_state_dict": self.optimizer.state_dict(),
            "total_timesteps": self.total_timesteps,
            "best_reward": self.best_reward,
        }, path)
        print(f"  Checkpoint saved: {path}")

    def _export_best(self):
        weights_path = self.output_dir / "policy_weights.bin"
        export_policy(self.policy, str(weights_path))
        print(f"  Exported C++ weights: {weights_path}")


def main():
    parser = argparse.ArgumentParser(description="Train UniCon policy with PPO")
    parser.add_argument("--config", type=str, default=None,
                        help="Path to JSON config file")
    parser.add_argument("--output", type=str, default="generated/unicon",
                        help="Output directory")
    parser.add_argument("--motions", type=str, default="assets/motions",
                        help="Motion capture directory")
    parser.add_argument("--iterations", type=int, default=None)
    parser.add_argument("--num-envs", type=int, default=None)
    parser.add_argument("--device", type=str, default=None)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    config = TrainingConfig()
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
    if args.device is not None:
        config.device = args.device

    np.random.seed(config.seed)
    torch.manual_seed(config.seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed(config.seed)

    trainer = UniConTrainer(config)
    trainer.train()


if __name__ == "__main__":
    main()
