"""Subprocess-based vectorized environment for parallel MuJoCo stepping.

Each worker process owns multiple UniConEnv instances (MuJoCo objects can't
be pickled, so envs are created inside the worker). Communication happens
via multiprocessing.Pipe with simple string commands.

Uses "spawn" context for macOS compatibility.
"""

import multiprocessing as mp
import os
from typing import List, Tuple

import numpy as np

from .config import (
    TrainingConfig, HumanoidConfig, RewardConfig, RSISConfig,
)
from .environment import UniConEnv
from tools.ml.config import PolicyConfig, PPOConfig
from tools.ml.motion_loader import load_motion_directory, generate_standing_motion


def _config_from_dict(d: dict) -> TrainingConfig:
    """Reconstruct TrainingConfig from a nested dict (from dataclasses.asdict)."""
    return TrainingConfig(
        humanoid=HumanoidConfig(**d["humanoid"]),
        policy=PolicyConfig(**d["policy"]),
        reward=RewardConfig(**d["reward"]),
        ppo=PPOConfig(**d["ppo"]),
        rsis=RSISConfig(**d["rsis"]),
        **{k: v for k, v in d.items()
           if k not in ("humanoid", "policy", "reward", "ppo", "rsis")},
    )


def _worker(
    pipe: mp.connection.Connection,
    config_dict: dict,
    env_indices: List[int],
    motion_dir: str,
    base_seed: int,
):
    """Worker process: creates envs locally and responds to commands.

    Each worker loads motion data independently (read-only files) and
    creates its own subset of environments.
    """
    config = _config_from_dict(config_dict)
    motion_data = load_motion_directory(motion_dir, config.humanoid.num_bodies)
    if not motion_data:
        motion_data = {"standing": generate_standing_motion(config.humanoid.num_bodies)}

    envs = [UniConEnv(config, motion_data=motion_data) for _ in env_indices]

    try:
        while True:
            cmd, data = pipe.recv()

            if cmd == "reset":
                obs_list = []
                for i, env in zip(env_indices, envs):
                    obs_list.append(env.reset(seed=base_seed + i))
                pipe.send(np.stack(obs_list))

            elif cmd == "step":
                actions = data  # shape (len(envs), act_dim)
                obs_list = []
                rewards = []
                dones = []
                infos = []

                for j, env in enumerate(envs):
                    ob, reward, done, info = env.step(actions[j])
                    if done:
                        info["episode_reward"] = info.get("episode_reward", reward)
                        info["episode_length"] = info.get("episode_length", 0)
                        ob = env.reset()
                    obs_list.append(ob)
                    rewards.append(reward)
                    dones.append(float(done))
                    infos.append(info)

                pipe.send((
                    np.stack(obs_list),
                    np.array(rewards, dtype=np.float32),
                    np.array(dones, dtype=np.float32),
                    infos,
                ))

            elif cmd == "get_dims":
                pipe.send((envs[0].obs_dim, envs[0].act_dim))

            elif cmd == "close":
                pipe.send(None)
                break
    except (EOFError, BrokenPipeError):
        pass


class SubprocVecEnv:
    """Vectorized environment using subprocess workers.

    Distributes environments across worker processes, each owning
    multiple envs to amortize IPC overhead.
    """

    def __init__(
        self,
        config: TrainingConfig,
        num_envs: int,
        num_workers: int,
        motion_dir: str,
        seed: int,
    ):
        self.num_envs = num_envs
        self.num_workers = min(num_workers, num_envs)

        # Serialize config as dict for pickling across processes
        config_dict = self._config_to_dict(config)

        # Distribute envs across workers
        env_assignments = self._distribute_envs(num_envs, self.num_workers)

        ctx = mp.get_context("spawn")
        self._parent_pipes = []
        self._workers = []
        self._env_counts = []

        idx = 0
        for worker_id in range(self.num_workers):
            count = env_assignments[worker_id]
            env_indices = list(range(idx, idx + count))
            idx += count
            self._env_counts.append(count)

            parent_conn, child_conn = ctx.Pipe()
            proc = ctx.Process(
                target=_worker,
                args=(child_conn, config_dict, env_indices, motion_dir, seed),
                daemon=True,
            )
            proc.start()
            child_conn.close()
            self._parent_pipes.append(parent_conn)
            self._workers.append(proc)

        # Get obs/act dims from first worker
        self._parent_pipes[0].send(("get_dims", None))
        self._obs_dim, self._act_dim = self._parent_pipes[0].recv()
        self._closed = False

    @property
    def obs_dim(self) -> int:
        return self._obs_dim

    @property
    def act_dim(self) -> int:
        return self._act_dim

    def reset(self) -> np.ndarray:
        """Reset all environments. Returns obs array of shape (num_envs, obs_dim)."""
        for pipe in self._parent_pipes:
            pipe.send(("reset", None))
        results = [pipe.recv() for pipe in self._parent_pipes]
        return np.concatenate(results, axis=0)

    def step(self, actions: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray, list]:
        """Step all environments in parallel.

        Args:
            actions: shape (num_envs, act_dim)

        Returns:
            obs: (num_envs, obs_dim)
            rewards: (num_envs,)
            dones: (num_envs,)
            infos: list of dicts, length num_envs
        """
        # Split actions by worker
        idx = 0
        for i, pipe in enumerate(self._parent_pipes):
            count = self._env_counts[i]
            pipe.send(("step", actions[idx:idx + count]))
            idx += count

        all_obs = []
        all_rewards = []
        all_dones = []
        all_infos = []
        for pipe in self._parent_pipes:
            obs, rewards, dones, infos = pipe.recv()
            all_obs.append(obs)
            all_rewards.append(rewards)
            all_dones.append(dones)
            all_infos.extend(infos)

        return (
            np.concatenate(all_obs, axis=0),
            np.concatenate(all_rewards, axis=0),
            np.concatenate(all_dones, axis=0),
            all_infos,
        )

    def close(self):
        """Shut down all worker processes."""
        if self._closed:
            return
        self._closed = True
        for pipe in self._parent_pipes:
            try:
                pipe.send(("close", None))
                pipe.recv()
                pipe.close()
            except (EOFError, BrokenPipeError, OSError):
                pass
        for proc in self._workers:
            proc.join(timeout=5)
            if proc.is_alive():
                proc.terminate()

    @staticmethod
    def _distribute_envs(num_envs: int, num_workers: int) -> List[int]:
        """Distribute envs as evenly as possible across workers."""
        base = num_envs // num_workers
        remainder = num_envs % num_workers
        return [base + (1 if i < remainder else 0) for i in range(num_workers)]

    @staticmethod
    def _config_to_dict(config: TrainingConfig) -> dict:
        """Convert dataclass config to a dict suitable for pickling."""
        from dataclasses import asdict
        return asdict(config)
