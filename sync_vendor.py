"""Keep PlatformIO's file:// library copies aligned with reviewed source."""
from pathlib import Path
import shutil

Import("env")
project = Path(env.subst("$PROJECT_DIR"))
libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
for name in ("BLESerial", "ELMDuino"):
    source = project / "vendor" / name
    target = libdeps / name
    # On the first build PlatformIO installs it itself. Refresh an existing
    # package before the dependency scan; file:// installations are snapshots.
    if not target.is_dir() or source.resolve() == target.resolve():
        continue
    for path in source.rglob("*"):
        if path.is_file():
            destination = target / path.relative_to(source)
            if not destination.is_file() or path.read_bytes() != destination.read_bytes():
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)
