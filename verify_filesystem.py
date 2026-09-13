"""Verify every expected file; older mklittlefs can exit zero after ENOSPC."""
from pathlib import Path
import hashlib, subprocess, sys, tempfile

def verify(source, image):
    source, image = Path(source).resolve(), Path(image).resolve()
    with tempfile.TemporaryDirectory(prefix='littlefs-check-') as scratch:
        subprocess.run([sys.executable, '-m', 'littlefs', 'extract', str(image), scratch,
                        '--block-size', '4096'], check=True)
        expected={p.relative_to(source).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in source.rglob('*') if p.is_file()}
        actual={p.relative_to(scratch).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                for p in Path(scratch).rglob('*') if p.is_file()}
        if not expected or actual != expected:
            raise RuntimeError('Filesystem image contents differ from data/: build is incomplete')
        print('Filesystem verified: %d files match source bytes.' % len(expected))

if __name__ == '__main__':
    verify(sys.argv[1], sys.argv[2])
