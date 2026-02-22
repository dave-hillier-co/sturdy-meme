"""AMP discriminator trainer for CALM.

WGAN-GP training of the adversarial motion prior discriminator.
The discriminator classifies (obs_t, obs_t1) transition pairs as
real (from motion clips) or fake (from policy rollouts).
"""

import torch
import torch.nn as nn
import torch.optim as optim
from typing import Tuple

from .config import AMPConfig, HumanoidCALMConfig
from .networks import AMPDiscriminator


class AMPTrainer:
    """Trains the AMP discriminator and computes style rewards.

    Uses WGAN-GP style training:
        L = E[D(fake)] - E[D(real)] + lambda * GP
    where GP is the gradient penalty on real samples.
    """

    def __init__(
        self,
        config: AMPConfig,
        humanoid: HumanoidCALMConfig,
        device: str = "cpu",
    ):
        self.config = config
        self.device = device

        self.discriminator = AMPDiscriminator(config, humanoid).to(device)
        self.optimizer = optim.Adam(
            self.discriminator.parameters(),
            lr=config.learning_rate,
        )

        # Replay buffer for fake transitions (from policy rollouts)
        self._fake_obs_t = []
        self._fake_obs_t1 = []
        self._max_replay = 100000

    def update(
        self,
        real_obs_t: torch.Tensor,
        real_obs_t1: torch.Tensor,
        fake_obs_t: torch.Tensor,
        fake_obs_t1: torch.Tensor,
    ) -> dict:
        """Run one discriminator update step.

        Args:
            real_obs_t, real_obs_t1: real transition pairs from motion buffer
            fake_obs_t, fake_obs_t1: fake transition pairs from policy rollouts

        Returns:
            dict with disc_loss, real_score, fake_score, grad_penalty
        """
        real_obs_t = real_obs_t.to(self.device)
        real_obs_t1 = real_obs_t1.to(self.device)
        fake_obs_t = fake_obs_t.to(self.device)
        fake_obs_t1 = fake_obs_t1.to(self.device)

        # Discriminator scores
        real_score = self.discriminator(real_obs_t, real_obs_t1)
        fake_score = self.discriminator(fake_obs_t, fake_obs_t1)

        # WGAN loss: maximize real, minimize fake
        disc_loss = fake_score.mean() - real_score.mean()

        # Gradient penalty on real samples
        grad_penalty = self._gradient_penalty(
            real_obs_t, real_obs_t1
        )

        total_loss = disc_loss + self.config.grad_penalty_weight * grad_penalty

        self.optimizer.zero_grad()
        total_loss.backward()
        self.optimizer.step()

        return {
            "disc_loss": disc_loss.item(),
            "real_score": real_score.mean().item(),
            "fake_score": fake_score.mean().item(),
            "grad_penalty": grad_penalty.item(),
        }

    def _gradient_penalty(
        self,
        obs_t: torch.Tensor,
        obs_t1: torch.Tensor,
    ) -> torch.Tensor:
        """Compute gradient penalty on real samples.

        GP = E[(||grad_D(x)||_2 - 1)^2] where x = real samples.
        """
        obs_t = obs_t.detach().requires_grad_(True)
        obs_t1 = obs_t1.detach().requires_grad_(True)

        score = self.discriminator(obs_t, obs_t1)
        ones = torch.ones_like(score)

        grads = torch.autograd.grad(
            outputs=score,
            inputs=[obs_t, obs_t1],
            grad_outputs=ones,
            create_graph=True,
            retain_graph=True,
        )

        grad_cat = torch.cat([g.reshape(g.shape[0], -1) for g in grads], dim=-1)
        grad_norm = grad_cat.norm(2, dim=-1)
        penalty = ((grad_norm - 1.0) ** 2).mean()
        return penalty

    def compute_style_reward(
        self,
        obs_t: torch.Tensor,
        obs_t1: torch.Tensor,
    ) -> torch.Tensor:
        """Compute AMP-style reward for PPO.

        reward = clamp(1 - 0.25 * (D(s,s') - 1)^2, 0, 1)
        """
        return self.discriminator.compute_reward(
            obs_t.to(self.device),
            obs_t1.to(self.device),
        )

    def add_fake_transitions(
        self,
        obs_t: torch.Tensor,
        obs_t1: torch.Tensor,
    ):
        """Add policy transitions to the replay buffer."""
        self._fake_obs_t.append(obs_t.detach().cpu())
        self._fake_obs_t1.append(obs_t1.detach().cpu())

        # Trim replay buffer
        total = sum(t.shape[0] for t in self._fake_obs_t)
        while total > self._max_replay and len(self._fake_obs_t) > 1:
            removed = self._fake_obs_t.pop(0)
            self._fake_obs_t1.pop(0)
            total -= removed.shape[0]

    def sample_fake_transitions(
        self, batch_size: int
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """Sample from the fake transition replay buffer."""
        if not self._fake_obs_t:
            dim = self.discriminator.network[0].in_features // 2
            return (
                torch.zeros(batch_size, dim),
                torch.zeros(batch_size, dim),
            )

        all_t = torch.cat(self._fake_obs_t, dim=0)
        all_t1 = torch.cat(self._fake_obs_t1, dim=0)
        n = all_t.shape[0]
        idx = torch.randint(0, n, (batch_size,))
        return all_t[idx], all_t1[idx]
