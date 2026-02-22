"""CALM reward computation.

Combines AMP style reward with task-specific rewards.
The style reward comes from the AMP discriminator.
Task rewards are defined per HLC task type.
"""

import torch


def combine_rewards(
    style_reward: torch.Tensor,
    task_reward: torch.Tensor,
    style_weight: float = 0.5,
    task_weight: float = 0.5,
) -> torch.Tensor:
    """Combine AMP style reward with task reward.

    r = w_style * r_style + w_task * r_task

    Args:
        style_reward: (batch,) AMP discriminator reward [0, 1]
        task_reward: (batch,) task-specific reward
        style_weight: weight for style reward
        task_weight: weight for task reward

    Returns:
        (batch,) combined reward
    """
    return style_weight * style_reward + task_weight * task_reward


def heading_task_reward(
    current_heading: torch.Tensor,
    target_heading: torch.Tensor,
    root_velocity: torch.Tensor,
    target_speed: float = 1.0,
) -> torch.Tensor:
    """Reward for matching a target heading direction and speed.

    Args:
        current_heading: (batch,) current facing angle
        target_heading: (batch,) target facing angle
        root_velocity: (batch, 3) root velocity in world space
        target_speed: desired forward speed

    Returns:
        (batch,) reward in [0, 1]
    """
    # Angular difference
    angle_diff = torch.abs(current_heading - target_heading)
    angle_diff = torch.min(angle_diff, 2 * 3.14159 - angle_diff)
    heading_reward = torch.exp(-2.0 * angle_diff)

    # Speed in target direction
    target_dir = torch.stack([
        torch.sin(target_heading),
        torch.zeros_like(target_heading),
        torch.cos(target_heading),
    ], dim=-1)
    forward_speed = (root_velocity * target_dir).sum(dim=-1)
    speed_reward = torch.exp(-2.0 * (forward_speed - target_speed) ** 2)

    return 0.5 * heading_reward + 0.5 * speed_reward


def location_task_reward(
    root_position: torch.Tensor,
    target_position: torch.Tensor,
) -> torch.Tensor:
    """Reward for reaching a target position.

    Args:
        root_position: (batch, 3) current root position
        target_position: (batch, 3) target position

    Returns:
        (batch,) reward in [0, 1]
    """
    dist = torch.norm(root_position - target_position, dim=-1)
    return torch.exp(-1.0 * dist)


def strike_task_reward(
    hand_position: torch.Tensor,
    target_position: torch.Tensor,
) -> torch.Tensor:
    """Reward for hand proximity to a strike target.

    Args:
        hand_position: (batch, 3) hand position
        target_position: (batch, 3) strike target position

    Returns:
        (batch,) reward in [0, 1]
    """
    dist = torch.norm(hand_position - target_position, dim=-1)
    return torch.exp(-5.0 * dist)
