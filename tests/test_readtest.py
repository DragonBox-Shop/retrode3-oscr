import imp
import binascii
import json
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/retrode3-readtest.py'
module = imp.load_source('retrode3_readtest', str(SCRIPT))


def read_bytes(path):
    with open(str(path), 'rb') as handle:
        return handle.read()


def write_json(path, data):
    with open(str(path), 'w') as handle:
        json.dump(data, handle)



class FakeBus:
    def __init__(self, data):
        self.data = data
        self.calls = []

    def read(self, mode, address, size):
        self.calls.append((mode, address, size))
        return self.data[(mode, address)][:size]


class TestReadTest(unittest.TestCase):
    def test_bad_candidate_crc_argument(self):
        class StubSlot:
            def __init__(self, system): pass
            def __enter__(self): return self
            def __exit__(self, *exc): pass
            def read(self, mode, address, size): return bytes([0x5a]) * size
        with tempfile.TemporaryDirectory() as tmp:
            db=Path(tmp)/'nes.json'
            write_json(db, {'records': []})
            with patch.object(module, 'Slot', StubSlot):
                self.assertEqual(module.main(['--system','nes','--action','read-rom',
                    '--database',str(db),'--match-crc32','nothex',
                    '--output',str(Path(tmp)/'x.nes')]),2)

    def test_nrom_header(self):
        x = bytes.fromhex('4e45531a020100000000000000000000')
        self.assertEqual(module.nrom_layout(x), (32768, 8192))
        with self.assertRaises(module.ReadError):
            module.nrom_layout(bytes.fromhex('4e45531a020110000000000000000000'))

    def test_nes_probe(self):
        a = bytes(range(256))*2
        b = bytes(reversed(range(256)))*2
        bus = FakeBus({('nes-prg', 0x8000):a, ('nes-prg', 0xe000):b})
        rec={'name':'Demo','id32a':module.crc(a),'id32b':module.crc(b),'crc32':'12345678'}
        info, found=module.detect_nes(bus,[rec])
        self.assertEqual(len(found),1)
        self.assertEqual(info['candidates'][0]['name'],'Demo')

    def test_nes_read(self):
        head=bytes.fromhex('4e45531a010100000000000000000000')
        prg=b'\x11'*16384
        chr_data=b'\x22'*8192
        bus=FakeBus({('nes-prg',0x8000):prg,('nes-chr',0):chr_data})
        value,ck=module.read_nes(bus,head)
        self.assertEqual(len(value),16+16384+8192)
        self.assertEqual(ck,module.crc(prg+chr_data))

    def test_md_header(self):
        d=bytearray(512)
        d[0x100:0x104]=b'SEGA'
        d[0x1a4:0x1a8]=(0x3ffff).to_bytes(4,'big')
        d[0x120:0x129]=b'JP TITLE '
        d[0x150:0x15a]=b'GAME NAME '
        d[0x180:0x18e]=b'GM DEMO123-00 '
        d[0x1f0:0x1f3]=b'JUE'
        bus=FakeBus({('md-rom',0):bytes(d)})
        info=module.detect_md(bus,'md-rom')
        self.assertEqual(info['header_size_bytes'],262144)
        self.assertEqual(info['domestic_title'], 'JP TITLE')
        self.assertEqual(info['overseas_title'], 'GAME NAME')
        self.assertEqual(info['serial'], 'GM DEMO123-00')
        self.assertEqual(info['region'], 'JUE')

    def test_manual_cli_dump(self):
        class DummySlot:
            def __init__(self, system): self.system=system
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, mode, address, size): return bytes([0x5a])*size
        with tempfile.TemporaryDirectory() as tmp:
            dest=Path(tmp)/'test.nes'
            with patch.object(module, 'Slot', DummySlot):
                result=module.main(['--system','nes','--action','read-rom', '--mapper','0',
                                    '--prg-kib','16','--chr-kib','8','--mirroring','horizontal',
                                    '--output',str(dest)])
            self.assertEqual(result,0)
            self.assertEqual(len(read_bytes(dest)),16+16384+8192)
            self.assertEqual(read_bytes(dest)[:4],b'NES\x1a')

    def test_md_read_cli(self):
        class DummySlot:
            def __init__(self, system): pass
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, mode, address, size):
                a=bytearray(size)
                if address==0 and size==512:
                    a[0x100:0x104]=b'SEGA'
                    a[0x1a4:0x1a8]=(1023).to_bytes(4,'big')
                return bytes(a)
        with tempfile.TemporaryDirectory() as tmp:
            dest=Path(tmp)/'md.bin'
            with patch.object(module, 'Slot', DummySlot):
                result=module.main(['--system','md','--action','read-rom','--output',str(dest)])
            self.assertEqual(result,0)
            self.assertEqual(len(read_bytes(dest)),1024)

    def test_slot_kernel_address_encoding(self):
        with patch.object(module.os, 'open', return_value=123), \
             patch.object(module.os, 'lseek', return_value=0) as seek, \
             patch.object(module.os, 'read', return_value=b'Z'), \
             patch.object(module.os, 'close'):
            with module.Slot('nes') as bus:
                self.assertEqual(bus.read('nes-prg', 0x8000, 1), b'Z')
        seek.assert_called_once_with(123, (10 << 24) | 0x8000, module.os.SEEK_SET)

    def test_auto_nes_dump_and_crc(self):
        import io
        from contextlib import redirect_stdout
        header=bytes.fromhex('4e45531a010100000000000000000000')
        prg=bytes((i // 64) & 255 for i in range(16384))
        chr_data=bytes((i * 19) & 255 for i in range(8192))
        first=prg[:512]
        last=prg[8192:8704]
        record={'name':'Known NROM Demo', 'mapper':0, 'submapper':0,
                'id32a':module.crc(first), 'id32b':module.crc(last),
                'crc32':module.crc(prg + chr_data), 'header':binascii.hexlify(header).decode('ascii')}
        class DummySlot:
            def __init__(self, system): pass
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, mode, address, size):
                if mode=='nes-prg':
                    if address==0x8000:
                        return prg if size==16384 else first
                    if address==0xe000:
                        return last
                if mode=='nes-chr': return chr_data
                raise AssertionError((mode,address,size))
        with tempfile.TemporaryDirectory() as tmp:
            dest=Path(tmp)/'known.nes'
            db=Path(tmp)/'nes.json'
            write_json(db, {'records':[record]})
            result_capture=io.StringIO()
            with patch.object(module,'Slot',DummySlot), redirect_stdout(result_capture):
                result=module.main(['--system','nes','--action','read-rom',
                                    '--database',str(db),'--output',str(dest)])
            self.assertEqual(result,0)
            info=json.loads(result_capture.getvalue())
            self.assertTrue(info['verified'])
            self.assertEqual(read_bytes(dest),header+prg+chr_data)

    def test_save_no_clobber(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'a.nes'
            module.save_atomic(p,b'hello')
            self.assertEqual(read_bytes(p),b'hello')
            with self.assertRaises(module.ReadError):
                module.save_atomic(p,b'evil')
            self.assertEqual(read_bytes(p),b'hello')


if __name__=='__main__':
    unittest.main()
