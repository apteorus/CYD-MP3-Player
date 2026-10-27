"""Integrity, unsupported-core and actual-link negative tests; no hardware writes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('audit',ROOT/'tools/verify_a2dp_link.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
parser=argparse.ArgumentParser()
parser.add_argument('--player-build',type=Path)
parser.add_argument('--uploader-build',type=Path)
args,remaining=parser.parse_known_args()

class Integration(unittest.TestCase):
    def test_corrupted_private_header_rejected(self):
        old=audit.VENDOR
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);good=b'// pinned SDK header\n'
            (path/'check.h').write_bytes(good)
            (path/'manifest.json').write_text(json.dumps({'idf_commit':'735507283d5b2f9fb363a1901172dbd9e847945d',
              'files':{'check.h':{'sha256':hashlib.sha256(good).hexdigest()}}}))
            try:
                audit.VENDOR=path;audit.check_sources()
                (path/'check.h').write_bytes(good+b'// accidental edit\n')
                with self.assertRaisesRegex(RuntimeError,'source mismatch'):
                    audit.check_sources()
            finally:
                audit.VENDOR=old

    def test_windows_line_directive_and_crlf(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'copied.c'
            path.write_bytes(b'#line 1 "C:\\Users\\name with spaces\\app\\source.c"\r\n// code\r\n')
            self.assertEqual(audit.compiled_digest(path),hashlib.sha256(b'// code\n').hexdigest())

    def test_windows_tool_suffix(self):
        entry={'arguments':['/tool path/xtensa-esp32-elf-gcc.exe']}
        self.assertEqual([x.name for x in audit.tool_paths(entry)],['xtensa-esp32-elf-nm.exe','xtensa-esp32-elf-objdump.exe'])

    @unittest.skipUnless(args.uploader_build,'provide --uploader-build for a real ELF negative test')
    def test_uploader_cannot_pass_player_audit(self):
        with self.assertRaisesRegex(RuntimeError,'wrong/missing map owner|missing patched symbol'):
            audit.verify(args.uploader_build)

    @unittest.skipUnless(args.player_build,'provide --player-build for real compiler version-guard test')
    def test_wrong_arduino_core_rejected_by_real_compiler(self):
        entries=json.loads((args.player_build/'compile_commands.json').read_text())
        entry=next(x for x in entries if x['file'].replace('\\','/').endswith('/src/idf_a2dp/btc_a2dp_source.c'))
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)
            (path/'esp_arduino_version.h').write_text('''#pragma once
#define ESP_ARDUINO_VERSION_VAL(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(3,3,9)
''')
            command=entry['arguments'].copy();index=command.index('-o');del command[index:index+2]
            command=[x for x in command if x not in ('-c','-MMD')]
            command[1:1]=['-I'+str(path),'-fsyntax-only']
            result=subprocess.run(command,cwd=entry['directory'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('ChonkPod A2DP override requires Arduino ESP32 3.3.10',result.stdout)

if __name__=='__main__':
    unittest.main(argv=[__file__,*remaining])
