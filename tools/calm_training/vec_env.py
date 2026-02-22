"""Subprocess-based vectorized environment for parallel CALM training.

Adapted from tools/unicon_training/vec_env.py for the CALM environment.
Each worker process owns multiple CALMEnv instances.

Uses "spawn" context for macOS compatibility.
"""

import multiprocessing as mp
import os
from dataclasses import asdict
from typing import List, Tuple

import numpy as np

from .config import (
    CALMConfig,
    HumanoidCALMConfig,
    LLCPolicyConfig,
    StyleMLPConfig,
    MainMLPConfig,
    MuHeadConfig,
    ValueConfig,
    AMPConfig,
    EncoderConfig,
    EncoderTrainingConfig,
    HLCConfig,
    HLCTaskConfig,
    EnvironmentConfig,
)
from tools.ml.config import PPOConfig
from tools.ml.motion_loader import load_motion_directory, generate_standing_motion


def _config_from_dict(d: dict) -> CALMConfig:
    """Reconstruct CALMConfig from a nested dict."""
    return CALMConfig(
        humanoid=HumanoidCALMConfig(**d["humanoid"]),
        llc_policy=LLCPolicyConfig(
            style=StyleMLPConfig(**d["llc_policy"]["style"]),
            main=MainMLPConfig(**d["llc_policy"]["main"]),
            mu_head=MuHeadConfig(**d["llc_policy"]["mu_head"]),
            log_std_init=d["llc_policy"]["log_std_init"],
        ),
        value=ValueConfig(**d["value"]),
        amp=AMPConfig(**d["amp"]),
        encoder=EncoderConfig(**d["encoder"]),
        encoder_training=EncoderTrainingConfig(**d["encoder_training"]),
        hlc=HLCConfig(
            heading=HLCTaskConfig(**d["hlc"]["heading"]),
            location=HLCTaskConfig(**d["hlc"]["location"]),
            strike=HLCTaskConfig(**d["hlc"]["strike"]),
        ),
        env=EnvironmentConfig(**d["env"]),
        ppo=PPOConfig(**d["ppo"]),
        **{k: v for k, v in d.items()
           if k not in ("humanoid", "llc_policy", "value", "amp", "encoder",
                        "encoder_training", "hlc", "env", "ppo")},
    )


def _worker(
    pipe: mp.connection.Connection,
    config_dict: dict,
    env_indices: List[int],
    motion_dir: str,
    base_seed: int,
):
    """Worker process: creates envs locally and responds to commands."""
    from .environment import CALMEnv

    config = _config_from_dict(config_dict)
    motion_data = load_motion_directory(motion_dir, 20)
    if not motion_data:
        motion_data = {"standing": generate_standing_motion(20)}

    envs = [CALMEnv(config, motion_data=motion_data) for _ in env_indices]

    try:
        while True:
            cmd, data = pipe.recv()

            if cmd == "reset":
                obs_list = []
                for i, env in zip(env_indices, envs):
                    obs_list.append(env.reset(seed=base_seed + i))
                pipe.send(np.stack(obs_list))

            elif cmd == "step":
                actions = data
                obs_list = []
                rewards = []
                dones = []
                infos = []

                for j, env in enumerate(envs):
                    ob, reward, done, info = env.step(actions[j])
                    if done:
                        info["episode_reward"] = reward
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


class CALMSubprocVecEnv:
    """Vectorized CALM environment using subprocess workers."""

    def __init__(
        self,
        config: CALMConfig,
        num_envs: int,
        num_workers: int,
        motion_dir: str,
        seed: int,
    ):
        self.num_envs = num_envs
        self.num_workers = min(num_workers, num_envs)

        config_dict = asdict(config)
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
        for pipe in self._parent_pipes:
            pipe.send(("reset", None))
        results = [pipe.recv() for pipe in self._parent_pipes]
        return np.concatenate(results, axis=0)

    def step(
        self, actions: np.ndarray
    ) -> Tuple[np.ndarray, np.ndarray, np.ndarray, list]:
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
        base = num_envs // num_workers
        remainder = num_envs % num_workers
        return [base + (1 if i < remainder else 0) for i in range(num_workers)]
