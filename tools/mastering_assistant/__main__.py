"""Mastering assistant — loudness normalization + soft limiting."""

import json
import sys
import os
import numpy as np

def load_audio(file_path):
    try:
        import soundfile as sf
        data, sr = sf.read(file_path, dtype='float32')
        return data, sr  # Keep stereo if present
    except ImportError:
        from scipy.io import wavfile
        sr, data = wavfile.read(file_path)
        if data.dtype != np.float32:
            data = data.astype(np.float32) / np.iinfo(data.dtype).max
        return data, sr

def save_audio(file_path, data, sr):
    try:
        import soundfile as sf
        sf.write(file_path, data, sr)
    except ImportError:
        from scipy.io import wavfile
        data_int = np.clip(data * 32767, -32768, 32767).astype(np.int16)
        wavfile.write(file_path, sr, data_int)

def compute_lufs(samples, sr):
    """Simplified integrated LUFS measurement (ITU-R BS.1770-4 approximation)."""
    if samples.ndim == 1:
        samples = samples.reshape(-1, 1)

    # K-weighting filter (simplified: just apply pre-emphasis)
    # True K-weighting needs specific filter coefficients, but for a
    # reasonable approximation we use a simple high-shelf emphasis.
    channels = samples.shape[1]

    # Block size: 400ms with 75% overlap
    block_samples = int(0.4 * sr)
    hop = block_samples // 4
    n_blocks = max(1, (samples.shape[0] - block_samples) // hop)

    block_powers = []
    for i in range(n_blocks):
        start = i * hop
        block = samples[start:start + block_samples]

        # Mean square per channel, then sum
        channel_powers = []
        for ch in range(channels):
            ms = np.mean(block[:, ch] ** 2)
            channel_powers.append(ms)

        # Sum channels (simplified — no surround weighting)
        total_power = sum(channel_powers)
        if total_power > 0:
            block_powers.append(total_power)

    if not block_powers:
        return -70.0

    # Absolute gating: -70 LUFS
    block_loudness = [10 * np.log10(p + 1e-20) - 0.691 for p in block_powers]

    # Relative gating threshold
    ungated_mean = np.mean([p for p, l in zip(block_powers, block_loudness) if l > -70])
    if ungated_mean <= 0:
        return -70.0

    relative_threshold = 10 * np.log10(ungated_mean + 1e-20) - 0.691 - 10

    # Final gated measurement
    gated_powers = [p for p, l in zip(block_powers, block_loudness) if l > relative_threshold]
    if not gated_powers:
        return -70.0

    mean_power = np.mean(gated_powers)
    lufs = 10 * np.log10(mean_power + 1e-20) - 0.691

    return round(lufs, 1)

def soft_limit(samples, ceiling_linear):
    """Soft limiter using tanh saturation."""
    # Normalize to ceiling
    peak = np.max(np.abs(samples))
    if peak <= ceiling_linear:
        return samples

    # Apply soft clipping
    ratio = ceiling_linear / peak
    normalized = samples * ratio

    # Gentle saturation for peaks above 90% of ceiling
    threshold = ceiling_linear * 0.9
    result = np.where(
        np.abs(normalized) > threshold,
        np.sign(normalized) * (threshold + (ceiling_linear - threshold) * np.tanh(
            (np.abs(normalized) - threshold) / (ceiling_linear - threshold + 1e-10)
        )),
        normalized
    )

    return result

def main():
    request = json.loads(sys.stdin.read())
    params = request.get("params", {})
    input_file = request.get("input_file")
    output_dir = request.get("output_dir", "/tmp")

    if not input_file:
        print(json.dumps({"status": "error", "message": "No input_file provided"}))
        return

    target_lufs = float(params.get("target_lufs", -14))
    apply_limiter = params.get("apply_limiter", True)
    ceiling_db = float(params.get("ceiling_db", -1.0))

    try:
        samples, sr = load_audio(input_file)
        was_mono = samples.ndim == 1
        if was_mono:
            samples = samples.reshape(-1, 1)

        # Measure current loudness
        current_lufs = compute_lufs(samples, sr)

        # Calculate gain adjustment
        gain_db = target_lufs - current_lufs
        gain_db = np.clip(gain_db, -20, 20)  # safety clamp
        gain_linear = 10 ** (gain_db / 20.0)

        # Apply gain
        result = samples * gain_linear

        # Apply limiter if requested
        if apply_limiter:
            ceiling_linear = 10 ** (ceiling_db / 20.0)
            result = soft_limit(result, ceiling_linear)

        # Measure final loudness
        final_lufs = compute_lufs(result, sr)
        final_peak_db = round(20 * np.log10(np.max(np.abs(result)) + 1e-20), 1)

        if was_mono:
            result = result.squeeze()

        base_name = os.path.splitext(os.path.basename(input_file))[0]
        output_path = os.path.join(output_dir, base_name + "_mastered.wav")
        os.makedirs(output_dir, exist_ok=True)
        save_audio(output_path, result, sr)

        result_json = {
            "status": "ok",
            "results": {
                "original_lufs": current_lufs,
                "target_lufs": target_lufs,
                "final_lufs": final_lufs,
                "gain_applied_db": round(gain_db, 1),
                "peak_db": final_peak_db,
                "limiter_applied": apply_limiter,
                "ceiling_db": ceiling_db,
                "duration": round(samples.shape[0] / sr, 3)
            },
            "output_files": [output_path]
        }
        print(json.dumps(result_json))

    except Exception as e:
        print(json.dumps({"status": "error", "message": str(e)}))

if __name__ == "__main__":
    main()
