#!/usr/bin/env python3
"""The one place the firmware's version comes from.

    git describe --tags --always --dirty

gives `v1.1.0` on a tagged commit, `v1.1.0-3-gabc1234` three commits past it,
and a bare `abc1234` in a checkout with no tags at all. The leading `v` is
dropped. That string reaches three places, and they can no longer disagree:

  - the panel's System footer, as -DFIRMWARE_VERSION injected at build time
    (this file is a PlatformIO pre-script: `extra_scripts = pre:...`);
  - the web installer's manifest, via `python3 tools/version.py`, which just
    prints it (CI does this);
  - GitHub Releases, which are cut by pushing the tag itself.

Nothing is bumped by hand. To release, tag: `git tag v1.2.0 && git push
origin v1.2.0`. Between tags the version says exactly how far past the last
release a build is.
"""
import os
import subprocess
import sys


def version(root: str) -> str:
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=root, capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"
    return out[1:] if out.startswith("v") else out


if __name__ == "__main__":
    print(version(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
else:
    # Run by PlatformIO (SCons) as a pre-script, where __file__ is not set;
    # the project directory is firmware/, and the repository is its parent.
    Import("env")  # noqa: F821 - provided by PlatformIO
    v = version(os.path.dirname(env.subst("$PROJECT_DIR")))  # noqa: F821
    env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(v))])  # noqa: F821
    print(f"version.py: FIRMWARE_VERSION={v}")
