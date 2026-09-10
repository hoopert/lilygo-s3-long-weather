#!/usr/bin/env python3
"""Decides the next release number, from git alone. No human in the loop.

Every merge to main is a release. The latest tag reachable from HEAD is the
current version; the commits since it decide the bump:

    [major] anywhere in a commit subject or body -> X+1.0.0
    [minor]                                      -> X.Y+1.0
    otherwise                                    -> X.Y.Z+1

A merge commit carries the pull request's title on its second line, so a
`[minor]` in a PR title is enough. With no tag at all the first release is
INITIAL (1.1.0: 1.0.0 was the hand-numbered firmware before the design pass).

Prints the bare number (no v). CI tags it; see .github/workflows/build.yml.
"""
import os
import re
import subprocess
import sys

INITIAL = "1.1.0"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                          check=True).stdout.strip()


def main() -> int:
    try:
        last_tag = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*")
    except subprocess.CalledProcessError:
        print(INITIAL)
        return 0

    m = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)", last_tag)
    if not m:
        print(f"last tag {last_tag!r} is not vX.Y.Z", file=sys.stderr)
        return 1
    major, minor, patch = (int(g) for g in m.groups())

    if git("rev-list", "-n", "1", f"{last_tag}..HEAD") == "":
        # HEAD is the tagged commit: nothing to release.
        print("")
        return 0

    log = git("log", f"{last_tag}..HEAD", "--format=%s%n%b")
    if "[major]" in log:
        major, minor, patch = major + 1, 0, 0
    elif "[minor]" in log:
        minor, patch = minor + 1, 0
    else:
        patch += 1
    print(f"{major}.{minor}.{patch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
