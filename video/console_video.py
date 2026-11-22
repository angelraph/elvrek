"""Elvrek: turn a crew console log recorded on the QNX target into a video.

The log (elv-console -l) holds every status line the console drew, ten a
second. console_replay draws each one again with the same code, and ffmpeg
encodes the frames at 10 per second, so the video runs at the real pace of the
run. Spot frames saved from the Screen buffer on the target are checked
against the replay pixel for pixel (see docs/CONSOLE.md).

Usage (from the repo root, after `make console` in WSL):
    python video/console_video.py build/con/log.txt video/capture/console.mp4
"""
import os
import subprocess
import sys

import imageio_ffmpeg

def wsl_path(p):
    p = os.path.abspath(p).replace("\\", "/")
    return "/mnt/" + p[0].lower() + p[2:] if p[1] == ":" else p


log, out = sys.argv[1], sys.argv[2]
rep = subprocess.Popen(
    ["wsl.exe", "-d", os.environ.get("ELV_WSL", "Ubuntu-24.04"), "--",
     wsl_path("build/console_replay"), wsl_path(log)],
    stdout=subprocess.PIPE)
ff = subprocess.run(
    [imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
     "-s", "1280x768", "-r", "10", "-i", "-", "-c:v", "libx264", "-crf", "16", "-preset", "medium",
     "-pix_fmt", "yuv420p", "-r", "30", out],
    stdin=rep.stdout)
rep.wait()
sys.exit(ff.returncode or rep.returncode)
