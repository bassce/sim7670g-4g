"""Portable build identification for git checkouts and downloaded source archives."""
import os
import subprocess
from pathlib import Path

Import("env")
root = Path(env.subst("$PROJECT_DIR"))

def git(*args):
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), *args], stderr=subprocess.DEVNULL, text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return ""

# Do not accidentally identify an enclosing workspace's unrelated git repository.
is_checkout = (root / ".git").exists()
revision = git("rev-parse", "--short=8", "HEAD") if is_checkout else ""
local_revision_file = root / "FIRMWARE_REVISION"
if not revision and local_revision_file.exists():
    revision = local_revision_file.read_text().strip()
if not revision and (root / "UPSTREAM_REVISION").exists():
    revision = (root / "UPSTREAM_REVISION").read_text().strip()[:8] + "-sim7670g-v2"
branch = os.getenv("RELEASE_VERSION") or (git("branch", "--show-current") if is_checkout else "") or ("main" if local_revision_file.exists() else "local")
def quoted(value):
    return env.StringifyMacro(value)

env.Append(CPPDEFINES=[("BUILD_GIT_BRANCH", quoted(branch)),
                      ("BUILD_GIT_COMMIT_HASH", quoted(revision or "unknown"))])
