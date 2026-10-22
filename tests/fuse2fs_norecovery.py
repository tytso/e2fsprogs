#!/usr/bin/env python3
"""Linux FUSE integration tests; retain images and logs in a new work directory.

Example: python3 tests/fuse2fs_norecovery.py --fuse2fs build/misc/fuse2fs \
    --work-dir "$HOME/fuse2fs-norecovery-test"
Requires mke2fs, debugfs, fusermount3, and permission to mount /dev/fuse.
"""
import argparse
from contextlib import contextmanager
import errno
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import unittest


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


class NoRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.directory = OPTIONS.work_dir / self._testMethodName
        self.directory.mkdir()
        self.image = self.directory / 'image.ext4'
        self.mountpoint = self.directory / 'mount'
        self.mountpoint.mkdir()
        seed = self.directory / 'seed'
        seed.mkdir()
        (seed / 'content').write_bytes(b'read-only image contents\n')
        self.mount_count = 0
        self.record = {'mounts': []}
        with (self.directory / 'mke2fs.log').open('wb') as log:
            subprocess.run([OPTIONS.mke2fs, '-q', '-t', 'ext4', '-b', '4096',
                            '-E', 'root_owner=%d:%d,lazy_itable_init=0,lazy_journal_init=0' %
                            (os.getuid(), os.getgid()), '-d', str(seed),
                            str(self.image), '8192'], stdout=log,
                           stderr=subprocess.STDOUT, check=True)

    def tearDown(self):
        # Never remove a directory which might still contain a live mount.
        self.record['still_mounted'] = os.path.ismount(self.mountpoint)
        self.record['final_sha256'] = digest(self.image)
        (self.directory / 'result.json').write_text(json.dumps(self.record, indent=2) + '\n')
        self.assertFalse(self.record['still_mounted'], 'mount was not cleaned up')

    def debugfs(self, request, writable=False):
        argv = [OPTIONS.debugfs] + (['-w'] if writable else [])
        result = subprocess.run(argv + ['-R', request, str(self.image)],
                                text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=True)
        with (self.directory / 'debugfs.log').open('a') as log:
            log.write(request + '\n' + result.stdout + result.stderr)
        return result.stdout

    @contextmanager
    def mounted(self, options):
        self.mount_count += 1
        logfile = self.directory / ('mount-%d.log' % self.mount_count)
        row = {'options': options, 'before_sha256': digest(self.image)}
        self.record['mounts'].append(row)
        with logfile.open('wb') as log:
            proc = subprocess.Popen([str(OPTIONS.fuse2fs), '-f', '-s', '-o', options,
                                     str(self.image), str(self.mountpoint)],
                                    stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 15
                while not os.path.ismount(self.mountpoint):
                    if proc.poll() is not None or time.monotonic() > deadline:
                        self.fail('mount failed: ' + logfile.read_text())
                    time.sleep(0.05)
                row['pid'] = proc.pid
                yield proc
            finally:
                if os.path.ismount(self.mountpoint):
                    subprocess.run([OPTIONS.fusermount, '-u', str(self.mountpoint)],
                                   check=True, timeout=15, stdout=log,
                                   stderr=subprocess.STDOUT)
                try:
                    row['returncode'] = proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    proc.wait(timeout=5)
                    raise
                row['after_sha256'] = digest(self.image)
        self.assertEqual(row['returncode'], 0, logfile.read_text())

    def assert_readonly(self, options):
        before = digest(self.image)
        with self.mounted(options) as proc:
            descriptors = []
            for entry in (Path('/proc') / str(proc.pid) / 'fd').iterdir():
                if os.readlink(entry) != str(self.image):
                    continue
                info = (Path('/proc') / str(proc.pid) / 'fdinfo' / entry.name).read_text()
                flags = next(int(line.split()[1], 8) for line in info.splitlines()
                             if line.startswith('flags:'))
                descriptors.append(flags)
            self.assertTrue(descriptors, 'no backing image descriptor found')
            self.assertTrue(all(flags & os.O_ACCMODE == os.O_RDONLY for flags in descriptors),
                            'backing image was opened writable')
            self.assertEqual((self.mountpoint / 'content').read_bytes(), b'read-only image contents\n')
            with self.assertRaises(OSError) as error:
                (self.mountpoint / 'forbidden').write_bytes(b'not allowed')
            self.assertEqual(error.exception.errno, errno.EROFS)
        self.assertEqual(before, digest(self.image), 'read-only mount modified the image')

    def test_clean_norecovery(self):
        self.assert_readonly('norecovery')

    def test_clean_noload_alias(self):
        self.assert_readonly('noload')

    def test_norecovery_overrides_rw_in_either_order(self):
        for options in ('rw,norecovery', 'norecovery,rw'):
            with self.subTest(options=options):
                self.assert_readonly(options)

    def test_write_protected_image(self):
        self.image.chmod(0o400)
        self.assert_readonly('norecovery')

    def test_pending_recovery_is_not_replayed(self):
        self.debugfs('feature needs_recovery', writable=True)
        self.assertIn('needs_recovery', self.debugfs('stats'))
        self.assert_readonly('norecovery')
        self.assertIn('needs_recovery', self.debugfs('stats'))

    def test_plain_ro_still_replays_journal(self):
        self.debugfs('feature needs_recovery', writable=True)
        self.assertIn('needs_recovery', self.debugfs('stats'))
        with self.mounted('ro'):
            self.assertEqual((self.mountpoint / 'content').read_bytes(), b'read-only image contents\n')
        self.assertNotIn('needs_recovery', self.debugfs('stats'))

    def test_read_error_does_not_modify_superblock(self):
        # Invalid directory record length/checksum makes readdir report corruption.
        block = int(self.debugfs('blocks /').split()[0])
        with self.image.open('r+b') as stream:
            stream.seek(block * 4096 + 4)
            stream.write(b'\x01\x00')
        before = digest(self.image)
        with self.mounted('norecovery,errors=continue'):
            with self.assertRaises(OSError):
                os.listdir(self.mountpoint)
        self.assertEqual(before, digest(self.image), 'error reporting modified the image')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fuse2fs', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--mke2fs', default='mke2fs')
    parser.add_argument('--debugfs', default='debugfs')
    parser.add_argument('--fusermount', default='fusermount3')
    OPTIONS, extra = parser.parse_known_args()
    OPTIONS.fuse2fs = OPTIONS.fuse2fs.resolve(strict=True)
    OPTIONS.work_dir = OPTIONS.work_dir.absolute()
    OPTIONS.work_dir.mkdir(parents=True)
    unittest.main(argv=[__file__, *extra], verbosity=2)
