import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class MMUTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        stub=Path(cls.temp.name)/'stub.c'
        stub.write_text('unsigned state; unsigned vflash_mmu_control(void){return state;} unsigned vflash_mmu_processor_state(void){return 0xd3;} void vflash_mmu_activate(unsigned t){(void)t;state=1;} void vflash_mmu_deactivate(void){state=0;}')
        output=Path(cls.temp.name)/'mmu.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC','-I',str(ROOT/'include'),str(ROOT/'src/platform/mmu.c'),str(stub),'-o',str(output)],check=True)
        cls.lib=ctypes.CDLL(str(output))
        cls.lib.vflash_mmu_descriptor.restype=ctypes.c_uint32
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def setUp(self):
        self.lib.vflash_mmu_disable();self.lib.vflash_mmu_init()
    def test_section_encoding_and_conflict_are_atomic(self):
        f=self.lib.vflash_mmu_map_section
        self.assertEqual(f(0x100000,0x200000,1),0)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x100000,1),0x200412)
        self.assertEqual(f(0x100000,0x300000,1),2)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x100000,1),0x200412)
        self.assertEqual(f(1,0,1),1)
        self.assertEqual(f(0,0,4),3)
    def test_page_permissions_capacity_unmap(self):
        f=self.lib.vflash_mmu_map_page
        for ap in (1,2,3):
            va=0x20000000+ap*4096
            self.assertEqual(f(va,0x10000000,ap),0)
            self.assertEqual(self.lib.vflash_mmu_descriptor(va,2),0x10000002+ap*0x550)
        self.assertEqual(f(0x20001000,0x10002000,1),2)
        self.assertEqual(self.lib.vflash_mmu_unmap_page(0x20001000),0)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x20001000,2),0)
        for va in (0x20100000,0x20200000,0x20300000):self.assertEqual(f(va,0x10000000,1),0)
        self.assertEqual(f(0x20400000,0x10000000,1),5)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x20400000,1),0)
    def test_identity_enable_and_active_edit_rejection(self):
        self.assertEqual(self.lib.vflash_mmu_enable(),6)
        self.assertEqual(self.lib.vflash_mmu_identity(),0)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x30000000,1),0)
        self.assertEqual(self.lib.vflash_mmu_enable(),0)
        self.assertEqual(self.lib.vflash_mmu_init(),4)
        self.assertEqual(self.lib.vflash_mmu_map_section(0x30000000,0,1),4)
        self.assertEqual(self.lib.vflash_mmu_map_page(0x30000000,0,1),4)
        self.assertEqual(self.lib.vflash_mmu_unmap_page(0x30000000),4)
