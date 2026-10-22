#!/usr/bin/env python3
"""Exercise inherited and pathname image locks on Linux with real FUSE mounts.

Run with --fuse2fs /path/to/fuse2fs --work-dir /new/directory.
Requires mke2fs, fusermount3, and permission to mount /dev/fuse.
Images and logs are retained for inspection, including on failure.
"""
import argparse
from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import unittest


def ofd_lock(fd, shared=False):
    kind = fcntl.F_RDLCK if shared else fcntl.F_WRLCK
    fcntl.fcntl(fd, fcntl.F_OFD_SETLK, struct.pack('hhqqi', kind, os.SEEK_SET, 0, 0, 0))


class InheritedImageLockTests(unittest.TestCase):
    def setUp(self):
        self.directory = OPTIONS.work_dir / self._testMethodName
        self.directory.mkdir()
        self.image = self.directory / 'image.ext4'
        self.mountpoint = self.directory / 'mount'
        self.mountpoint.mkdir()
        seed = self.directory / 'seed'
        seed.mkdir()
        (seed / 'content').write_bytes(b'inherited image contents\n')
        self.record = {}
        with (self.directory / 'mke2fs.log').open('wb') as log:
            subprocess.run([OPTIONS.mke2fs, '-q', '-t', 'ext4', '-b', '4096',
                            '-E', 'root_owner=%d:%d,lazy_itable_init=0,lazy_journal_init=0' %
                            (os.getuid(), os.getgid()), '-d', str(seed),
                            str(self.image), '8192'], stdout=log,
                           stderr=subprocess.STDOUT, check=True)

    def tearDown(self):
        self.record['still_mounted'] = os.path.ismount(self.mountpoint)
        (self.directory / 'result.json').write_text(json.dumps(self.record, indent=2) + '\n')
        self.assertFalse(self.record['still_mounted'], 'mount was not cleaned up')

    @contextmanager
    def inherited(self, kind, shared=False):
        fd = os.open(self.image, os.O_RDONLY if shared else os.O_RDWR)
        try:
            if kind == 'ofd':
                if not hasattr(fcntl, 'F_OFD_SETLK'):
                    self.skipTest('Python or kernel lacks OFD locks')
                ofd_lock(fd, shared)
            else:
                fcntl.flock(fd, (fcntl.LOCK_SH if shared else fcntl.LOCK_EX) | fcntl.LOCK_NB)
            yield fd
        finally:
            os.close(fd)

    @contextmanager
    def mounted(self, fd=None, options='rw', device_suffix=''):
        device = str(self.image) if fd is None else '/proc/self/fd/%d%s' % (fd, device_suffix)
        logfile = self.directory / 'mount.log'
        with logfile.open('wb') as log:
            proc = subprocess.Popen([str(OPTIONS.fuse2fs), '-f', '-s', '-o', options,
                                     device, str(self.mountpoint)],
                                    pass_fds=() if fd is None else (fd,),
                                    stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 30
                while not os.path.ismount(self.mountpoint):
                    if proc.poll() is not None or time.monotonic() > deadline:
                        self.fail('mount failed: ' + logfile.read_text())
                    time.sleep(0.05)
                self.record['mounted'] = True
                self.assertEqual((self.mountpoint / 'content').read_bytes(), b'inherited image contents\n')
                yield proc
            finally:
                if os.path.ismount(self.mountpoint):
                    subprocess.run([OPTIONS.fusermount, '-u', str(self.mountpoint)],
                                   check=True, timeout=15, stdout=log,
                                   stderr=subprocess.STDOUT)
                try:
                    self.record['returncode'] = proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    proc.wait(timeout=5)
                    raise
        self.assertEqual(self.record['returncode'], 0, logfile.read_text())

    def assert_bsd_lock(self, shared, allowed):
        fd = os.open(self.image, os.O_RDONLY if shared else os.O_RDWR)
        try:
            operation = (fcntl.LOCK_SH if shared else fcntl.LOCK_EX) | fcntl.LOCK_NB
            if allowed:
                fcntl.flock(fd, operation)
            else:
                with self.assertRaises(BlockingIOError):
                    fcntl.flock(fd, operation)
        finally:
            os.close(fd)

    def assert_released(self):
        self.assert_bsd_lock(shared=False, allowed=True)
        self.record['released_after_all_descriptors_closed'] = True

    def write_and_sync(self):
        with (self.mountpoint / 'written').open('wb') as stream:
            stream.write(b'locked writable mount\n')
            stream.flush()
            os.fsync(stream.fileno())

    def test_inherited_bsd_writer(self):
        with self.inherited('bsd') as fd:
            with self.mounted(fd):
                self.write_and_sync()
                self.assert_bsd_lock(shared=True, allowed=False)
                self.assert_bsd_lock(shared=False, allowed=False)
        self.assert_released()

    def test_inherited_ofd_writer(self):
        with self.inherited('ofd') as fd:
            with self.mounted(fd):
                self.write_and_sync()
                self.assert_bsd_lock(shared=False, allowed=False)
                other = os.open(self.image, os.O_RDWR)
                try:
                    with self.assertRaises(BlockingIOError):
                        ofd_lock(other)
                finally:
                    os.close(other)
        self.assert_released()

    @unittest.skipIf(os.geteuid() == 0, 'read-only fallback needs an unprivileged user')
    def test_shared_inherited_reader(self):
        self.image.chmod(0o400)
        with self.inherited('ofd', shared=True) as fd:
            with self.mounted(fd, 'ro'):
                self.assert_bsd_lock(shared=True, allowed=True)
                self.image.chmod(0o600)
                self.assert_bsd_lock(shared=False, allowed=False)
        self.assert_released()

    def test_competing_pathname_mount_is_rejected(self):
        other = self.directory / 'other-mount'
        other.mkdir()
        with self.inherited('ofd') as fd:
            with self.mounted(fd):
                logfile = self.directory / 'competitor.log'
                try:
                    with logfile.open('wb') as log:
                        result = subprocess.run([str(OPTIONS.fuse2fs), '-f', '-s',
                                                 str(self.image), str(other)], stdout=log,
                                                stderr=subprocess.STDOUT, timeout=30)
                    self.assertEqual(result.returncode, 32, logfile.read_text())
                    self.assertIn('Could not lock filesystem image', logfile.read_text())
                    self.assertFalse(os.path.ismount(other))
                finally:
                    if os.path.ismount(other):
                        subprocess.run([OPTIONS.fusermount, '-u', str(other)], check=True, timeout=15)
        self.assert_released()

    def test_ordinary_pathname_still_excludes_writers(self):
        with self.mounted():
            self.write_and_sync()
            self.assert_bsd_lock(shared=False, allowed=False)
        self.assert_released()

    def test_path_relative_to_directory_descriptor(self):
        fd = os.open(self.directory, os.O_RDONLY | os.O_DIRECTORY)
        try:
            with self.mounted(fd, device_suffix='/image.ext4'):
                self.write_and_sync()
                self.assert_bsd_lock(shared=False, allowed=False)
        finally:
            os.close(fd)
        self.assert_released()

    def test_inherited_descriptor_with_image_offset(self):
        original = self.image
        self.image = self.directory / 'offset-image.bin'
        header = b'untouched prefix' * 256
        self.assertEqual(len(header), 4096)
        with self.image.open('wb') as output, original.open('rb') as source:
            output.write(header)
            while chunk := source.read(1024 * 1024):
                output.write(chunk)
        with self.inherited('bsd') as fd:
            with self.mounted(fd, 'offset=4096'):
                self.write_and_sync()
                self.assert_bsd_lock(shared=False, allowed=False)
        with self.image.open('rb') as stream:
            self.assertEqual(stream.read(4096), header)
        self.assert_released()

    def test_daemon_retains_lock_passed_on_stdin(self):
        fd = os.open(self.image, os.O_RDWR)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            logfile = self.directory / 'daemon.log'
            with logfile.open('wb') as log:
                result = subprocess.run([str(OPTIONS.fuse2fs), '/proc/self/fd/0',
                                         str(self.mountpoint)], stdin=fd,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=30)
            self.assertEqual(result.returncode, 0, logfile.read_text())
            os.close(fd)
            fd = -1
            deadline = time.monotonic() + 10
            while not os.path.ismount(self.mountpoint):
                if time.monotonic() > deadline:
                    self.fail('daemon did not mount')
                time.sleep(0.05)
            self.assertEqual((self.mountpoint / 'content').read_bytes(), b'inherited image contents\n')
            self.assert_bsd_lock(shared=False, allowed=False)
        finally:
            if fd >= 0:
                os.close(fd)
            if os.path.ismount(self.mountpoint):
                subprocess.run([OPTIONS.fusermount, '-u', str(self.mountpoint)], check=True, timeout=15)
        deadline = time.monotonic() + 10
        while True:
            try:
                self.assert_released()
                break
            except BlockingIOError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.05)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fuse2fs', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--mke2fs', default='mke2fs')
    parser.add_argument('--fusermount', default='fusermount3')
    OPTIONS, extra = parser.parse_known_args()
    OPTIONS.fuse2fs = OPTIONS.fuse2fs.resolve(strict=True)
    OPTIONS.work_dir = OPTIONS.work_dir.absolute()
    OPTIONS.work_dir.mkdir(parents=True)
    unittest.main(argv=[__file__, *extra], verbosity=2)
