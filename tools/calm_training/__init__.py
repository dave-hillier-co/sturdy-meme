"""CALM (Conditional Adversarial Latent Models) training pipeline.

Three-phase training:
  Phase 1: LLC (Low-Level Controller) via PPO + AMP discriminator
  Phase 2: Encoder via contrastive learning (frozen LLC)
  Phase 3: HLC (High-Level Controllers) via task PPO (frozen LLC)
"""
