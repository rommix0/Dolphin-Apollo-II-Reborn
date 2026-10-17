import os
# Unicorn harness: run Orpheus synth routines from a3s.dll in isolation (no Windows, no native load).
import pefile, struct, sys
from unicorn import *
from unicorn.x86_const import *
F=os.environ.get("ORPHEUS_A3S", r"C:\Users\abart\Desktop\orpheus-classic-sapi5-main\bin\orpheus\a3s.dll")
pe=pefile.PE(F); IB=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
SIZE=(len(img)+0xffff)&~0xffff
mu=Uc(UC_ARCH_X86,UC_MODE_32)
mu.mem_map(IB,SIZE); mu.mem_write(IB,img)
STK=0x00100000; mu.mem_map(STK,0x200000)
HEAP=0x10000000; mu.mem_map(HEAP,0x8000000)
TEB=0x7ff00000; mu.mem_map(TEB,0x10000)
STUB=0x7fe00000; mu.mem_map(STUB,0x10000)
SENT=STUB+0xff00
mu.mem_write(SENT,b'\xf4')
GDT=0x7fd00000; mu.mem_map(GDT,0x1000)
def gdt_entry(base,limit,access,flags):
    e=limit&0xffff
    e|=(base&0xffffff)<<16
    e|=(access&0xff)<<40
    e|=((limit>>16)&0xf)<<48
    e|=(flags&0xf)<<52
    e|=((base>>24)&0xff)<<56
    return struct.pack('<Q',e)
mu.mem_write(GDT+8*1,gdt_entry(0,0xfffff,0x9a,0xc))      # code ring0
mu.mem_write(GDT+8*2,gdt_entry(0,0xfffff,0x92,0xc))      # data ring0
mu.mem_write(GDT+8*3,gdt_entry(TEB,0xfff,0x92,0x4))      # fs
mu.reg_write(UC_X86_REG_GDTR,(0,GDT,0x1000,0))
mu.reg_write(UC_X86_REG_SS,2<<3)
mu.reg_write(UC_X86_REG_DS,2<<3); mu.reg_write(UC_X86_REG_ES,2<<3)
mu.reg_write(UC_X86_REG_FS,3<<3)
# Unicorn starts the x87 at control word 0 (24-bit precision). A Windows thread running MSVC code starts at the CRT's
# default 0x027F (53-bit, round to nearest, exceptions masked); the synth's own fninit later sets 0x037F.
mu.reg_write(UC_X86_REG_FPCW,0x027F)
mu.mem_write(TEB,struct.pack('<I',0xffffffff))
heap_ptr=[HEAP]
TLSBLK=TEB+0x1000
def alloc(n):
    p=heap_ptr[0]; heap_ptr[0]=(p+n+15)&~15; return p
# IAT -> stubs
stubs={}
k=0
for d in pe.DIRECTORY_ENTRY_IMPORT:
    for i in d.imports:
        a=STUB+k*16; k+=1
        stubs[a]=(i.name or b'?').decode()
        mu.mem_write(i.address,struct.pack('<I',a)); mu.mem_write(a,b'\xc3')
def r32(a): return struct.unpack('<I',mu.mem_read(a,4))[0]
def hook_code(uc,addr,size,ud):
    if addr in stubs:
        name=stubs[addr]; esp=uc.reg_read(UC_X86_REG_ESP); ret=r32(esp)
        if name=='HeapAlloc':
            n=r32(esp+12); p=alloc(n); uc.mem_write(p,bytes(n)); res,na=p,3
        elif name=='HeapFree': res,na=1,3
        elif name in ('EnterCriticalSection','LeaveCriticalSection','InitializeCriticalSection',
                      'DeleteCriticalSection','SetLastError'):
            res,na=0,1
        elif name=='TlsGetValue': res,na=TLSBLK,1
        elif name=='GetLastError': res,na=0,0
        elif name=='GetCurrentThreadId': res,na=1,0
        else:
            print("IMPORT CALLED",name); uc.emu_stop(); return
        uc.reg_write(UC_X86_REG_EAX,res); uc.reg_write(UC_X86_REG_ESP,esp+4+4*na); uc.reg_write(UC_X86_REG_EIP,ret)
    elif addr in (0x23211330,0x23211ef0):   # operator new(size) / _nh_malloc(size,flag): cdecl
        esp=uc.reg_read(UC_X86_REG_ESP); n=r32(esp+4); ret=r32(esp)
        p=alloc(n); uc.mem_write(p,b'\0'*n)
        uc.reg_write(UC_X86_REG_EAX,p); uc.reg_write(UC_X86_REG_ESP,esp+4); uc.reg_write(UC_X86_REG_EIP,ret)
mu.hook_add(UC_HOOK_CODE,hook_code,begin=0x23211330,end=0x23211330)
mu.hook_add(UC_HOOK_CODE,hook_code,begin=0x23211ef0,end=0x23211ef0)
mu.hook_add(UC_HOOK_CODE,hook_code,begin=STUB,end=STUB+0xfe00)
def bad(uc,access,addr,size,value,ud):
    print("BAD MEM",access,hex(addr),"eip",hex(uc.reg_read(UC_X86_REG_EIP))); return False
mu.hook_add(UC_HOOK_MEM_INVALID,bad)
def call(fn,this=0,args=()):
    esp=STK+0x1f0000
    for a in reversed(args): esp-=4; mu.mem_write(esp,struct.pack('<I',a&0xffffffff))
    esp-=4; mu.mem_write(esp,struct.pack('<I',SENT))
    mu.reg_write(UC_X86_REG_ESP,esp); mu.reg_write(UC_X86_REG_ECX,this)
    mu.emu_start(fn,SENT)
    return mu.reg_read(UC_X86_REG_EAX)
def rd(a,n): return bytes(mu.mem_read(a,n))
def dbl(a): return struct.unpack('<d',rd(a,8))[0]
def i16(a): return struct.unpack('<h',rd(a,2))[0]
def make_synth(rate):
    # A fresh synth starts as on a fresh Windows thread: a previous one's fninit left the x87 at 0x037F.
    mu.reg_write(UC_X86_REG_FPCW,0x027F)
    obj=alloc(0xb168); mu.mem_write(obj,b'\0'*0xb168)
    call(0x2320bb10,obj,(rate,))
    call(0x2320c620,obj,(rate,))
    call(0x2320c510,obj)
    return obj
if __name__=='__main__':
    rate=int(sys.argv[1]) if len(sys.argv)>1 else 11025
    o=make_synth(rate)
    cfg=r32(o+0x28)
    print("obj",hex(o),"cfg",hex(cfg),"cfg[0]",r32(cfg),"1dc",i16(o+0x1dc),"1de",i16(o+0x1de),"1e0",i16(o+0x1e0))
