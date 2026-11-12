from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/preprocess_bad_apple.py"


class BadApplePipelineTests(unittest.TestCase):
    def test_pipeline_with_synthetic_media_or_local_cache(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            real_source = ROOT / "assets/local/bad-apple/source.mp4"

            if real_source.exists():
                # Test a fast 2-second slice of real media
                cmd = [
                    "python3", str(SCRIPT),
                    "--source", str(real_source),
                    "--out-dir", str(tmp_dir),
                    "--clip", "2.0",
                    "--only", "160x120@24",
                ]
                subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

                mjp = tmp_dir / "bad_apple_160x120_24fps.mjp"
                self.assertTrue(mjp.is_file())
                self.assertGreater(mjp.stat().st_size, 10000)
            else:
                # Synthetic fallback test
                synth_mp4 = tmp_dir / "synthetic.mp4"
                cmd_synth = [
                    "ffmpeg", "-y", "-f", "lavfi",
                    "-i", "testsrc=duration=2:size=320x240:rate=15",
                    "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
                    "-c:v", "libx264", "-c:a", "aac",
                    str(synth_mp4)
                ]
                subprocess.run(cmd_synth, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

                cmd = [
                    "python3", str(SCRIPT),
                    "--source", str(synth_mp4),
                    "--out-dir", str(tmp_dir),
                    "--only", "160x120@15",
                ]
                subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
                mjp = tmp_dir / "bad_apple_160x120_15fps.mjp"
                self.assertTrue(mjp.is_file())


if __name__ == "__main__":
    unittest.main()
