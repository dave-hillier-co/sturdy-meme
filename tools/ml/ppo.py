"""General-purpose PPO (Proximal Policy Optimization) implementation.

Provides RolloutBuffer and a PPO update step that work with any
environment producing (obs, reward, done) tuples.
"""

import torch
import torch.nn as nn
import torch.optim as optim

from .config import PPOConfig


class RolloutBuffer:
    """Stores trajectory data for PPO updates."""

    def __init__(self, num_envs: int, horizon: int, obs_dim: int, act_dim: int,
                 device: str = "cpu"):
        self.num_envs = num_envs
        self.horizon = horizon
        self.device = device

        self.observations = torch.zeros(horizon, num_envs, obs_dim, device=device)
        self.actions = torch.zeros(horizon, num_envs, act_dim, device=device)
        self.log_probs = torch.zeros(horizon, num_envs, device=device)
        self.rewards = torch.zeros(horizon, num_envs, device=device)
        self.dones = torch.zeros(horizon, num_envs, device=device)
        self.values = torch.zeros(horizon, num_envs, device=device)
        self.advantages = torch.zeros(horizon, num_envs, device=device)
        self.returns = torch.zeros(horizon, num_envs, device=device)

        self.step = 0

    def add(self, obs, action, log_prob, reward, done, value):
        self.observations[self.step] = obs
        self.actions[self.step] = action
        self.log_probs[self.step] = log_prob
        self.rewards[self.step] = reward
        self.dones[self.step] = done
        self.values[self.step] = value
        self.step += 1

    def compute_gae(self, last_value: torch.Tensor, gamma: float, gae_lambda: float):
        """Compute generalized advantage estimation."""
        last_gae = 0
        for t in reversed(range(self.horizon)):
            if t == self.horizon - 1:
                next_value = last_value
            else:
                next_value = self.values[t + 1]
            next_non_terminal = 1.0 - self.dones[t]
            delta = self.rewards[t] + gamma * next_value * next_non_terminal - self.values[t]
            self.advantages[t] = last_gae = delta + gamma * gae_lambda * next_non_terminal * last_gae
        self.returns = self.advantages + self.values

    def flatten(self):
        """Flatten (horizon, num_envs, ...) -> (horizon * num_envs, ...)."""
        batch_size = self.horizon * self.num_envs
        return {
            "observations": self.observations.reshape(batch_size, -1),
            "actions": self.actions.reshape(batch_size, -1),
            "log_probs": self.log_probs.reshape(batch_size),
            "advantages": self.advantages.reshape(batch_size),
            "returns": self.returns.reshape(batch_size),
        }

    def reset(self):
        self.step = 0


def ppo_update(policy, value_net, optimizer, buffer: RolloutBuffer,
               config: PPOConfig) -> dict:
    """Run PPO update epochs on collected rollout data.

    Args:
        policy: MLPPolicy with evaluate_actions(obs, actions) method.
        value_net: ValueNetwork with forward(obs) method.
        optimizer: torch.optim.Optimizer for both networks.
        buffer: filled RolloutBuffer.
        config: PPO hyperparameters.

    Returns:
        dict with policy_loss, value_loss, entropy, approx_kl averages.
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
            mb_indices = indices[start:end]

            mb_obs = data["observations"][mb_indices]
            mb_actions = data["actions"][mb_indices]
            mb_old_log_probs = data["log_probs"][mb_indices]
            mb_advantages = adv[mb_indices]
            mb_returns = data["returns"][mb_indices]

            new_log_probs, entropy = policy.evaluate_actions(mb_obs, mb_actions)
            new_values = value_net(mb_obs)

            # Clipped surrogate objective
            ratio = (new_log_probs - mb_old_log_probs).exp()
            surr1 = ratio * mb_advantages
            surr2 = torch.clamp(ratio, 1 - config.clip_epsilon,
                                1 + config.clip_epsilon) * mb_advantages
            policy_loss = -torch.min(surr1, surr2).mean()

            value_loss = nn.functional.mse_loss(new_values, mb_returns)
            entropy_loss = -entropy.mean()

            loss = (policy_loss
                    + config.value_loss_coeff * value_loss
                    + config.entropy_coeff * entropy_loss)

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
