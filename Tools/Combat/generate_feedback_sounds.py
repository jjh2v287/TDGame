"""실행 환경: 시스템 Python 3.12 + numpy(에디터 밖). 전투 표현용 효과음 WAV를 결정론 합성으로 만든다(같은 시드면 같은 파일).
실행 명령: python Tools/Combat/generate_feedback_sounds.py [--out AudioSources/Combat]
출력: <out>/SW_TD_CritAccent.wav·SW_TD_KillThud.wav·SW_TD_BodyFall.wav·SW_TD_HeavyImpact.wav(48kHz 16비트 모노), 표준 출력에 파일별 길이·최대값
상태: 현행 (2026-10-18, M3-27)
"""
import argparse
import os
import wave

import numpy as np

SAMPLE_RATE = 48000


def seconds_to_samples(seconds):
    return int(round(seconds * SAMPLE_RATE))


def time_axis(seconds):
    return np.arange(seconds_to_samples(seconds)) / SAMPLE_RATE


def one_pole_lowpass(signal, cutoff_hz):
    alpha = 1.0 - np.exp(-2.0 * np.pi * cutoff_hz / SAMPLE_RATE)
    filtered = np.empty_like(signal)
    state = 0.0
    for index, value in enumerate(signal):
        state += alpha * (value - state)
        filtered[index] = state
    return filtered


def one_pole_highpass(signal, cutoff_hz):
    return signal - one_pole_lowpass(signal, cutoff_hz)


def decay_envelope(seconds, attack_seconds, decay_seconds):
    t = time_axis(seconds)
    attack = np.clip(t / max(attack_seconds, 1e-6), 0.0, 1.0)
    return attack * np.exp(-t / decay_seconds)


def swept_sine(seconds, start_hz, end_hz, sweep_seconds):
    t = time_axis(seconds)
    progress = np.clip(t / sweep_seconds, 0.0, 1.0)
    frequency = end_hz + (start_hz - end_hz) * (1.0 - progress) ** 2
    phase = 2.0 * np.pi * np.cumsum(frequency) / SAMPLE_RATE
    return np.sin(phase)


def noise(seconds, generator):
    return generator.uniform(-1.0, 1.0, seconds_to_samples(seconds))


def place(buffer, signal, start_seconds):
    start = seconds_to_samples(start_seconds)
    end = min(len(buffer), start + len(signal))
    buffer[start:end] += signal[: end - start]


def normalize(signal, peak=0.89):
    maximum = np.max(np.abs(signal))
    if maximum <= 0.0:
        return signal
    return signal * (peak / maximum)


def make_crit_accent(generator):
    length = 0.45
    t = time_axis(length)
    partials = [(1760.0, 1.0, 0.16), (2793.0, 0.55, 0.11), (4186.0, 0.32, 0.07), (5920.0, 0.18, 0.05)]
    ping = np.zeros_like(t)
    for frequency, gain, decay in partials:
        ping += gain * np.sin(2.0 * np.pi * frequency * t * (1.0 - 0.04 * t)) * np.exp(-t / decay)
    click = one_pole_highpass(noise(length, generator), 2500.0) * decay_envelope(length, 0.0005, 0.006)
    body = swept_sine(length, 420.0, 180.0, 0.08) * decay_envelope(length, 0.001, 0.05)
    return normalize(0.55 * ping + 0.6 * click + 0.35 * body)


def make_kill_thud(generator):
    length = 0.7
    thump = swept_sine(length, 140.0, 42.0, 0.18) * decay_envelope(length, 0.002, 0.16)
    crack = one_pole_lowpass(noise(length, generator), 3200.0) * decay_envelope(length, 0.0008, 0.025)
    rumble = one_pole_lowpass(noise(length, generator), 260.0) * decay_envelope(length, 0.01, 0.22)
    return normalize(1.0 * thump + 0.45 * crack + 1.6 * rumble)


def make_body_fall(generator):
    length = 0.9
    buffer = np.zeros(seconds_to_samples(length))
    for start, gain, pitch in [(0.0, 1.0, 1.0), (0.13, 0.55, 1.25), (0.24, 0.3, 1.5)]:
        segment_length = length - start
        hit = swept_sine(segment_length, 110.0 * pitch, 50.0 * pitch, 0.1) * decay_envelope(segment_length, 0.002, 0.09)
        dirt = one_pole_lowpass(noise(segment_length, generator), 1800.0) * decay_envelope(segment_length, 0.001, 0.05)
        place(buffer, gain * (hit + 0.5 * dirt), start)
    return normalize(buffer)


def make_heavy_impact(generator):
    length = 0.4
    punch = swept_sine(length, 180.0, 70.0, 0.06) * decay_envelope(length, 0.001, 0.07)
    snap = one_pole_highpass(noise(length, generator), 1200.0) * decay_envelope(length, 0.0005, 0.012)
    grit = one_pole_lowpass(noise(length, generator), 900.0) * decay_envelope(length, 0.002, 0.06)
    return normalize(1.0 * punch + 0.7 * snap + 0.6 * grit)


def write_wave(path, signal):
    fade = min(len(signal), seconds_to_samples(0.004))
    signal = signal.copy()
    signal[-fade:] *= np.linspace(1.0, 0.0, fade)
    samples = np.clip(signal * 32767.0, -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(samples.tobytes())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join("AudioSources", "Combat"))
    arguments = parser.parse_args()
    os.makedirs(arguments.out, exist_ok=True)
    makers = {
        "SW_TD_CritAccent": make_crit_accent,
        "SW_TD_KillThud": make_kill_thud,
        "SW_TD_BodyFall": make_body_fall,
        "SW_TD_HeavyImpact": make_heavy_impact,
    }
    for seed, (name, maker) in enumerate(makers.items(), start=1827):
        signal = maker(np.random.default_rng(seed))
        path = os.path.join(arguments.out, name + ".wav")
        write_wave(path, signal)
        print(f"{name}: {len(signal) / SAMPLE_RATE:.3f}s peak={np.max(np.abs(signal)):.3f} -> {path}")


if __name__ == "__main__":
    main()
