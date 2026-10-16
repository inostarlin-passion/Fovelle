#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Generate an original, uncompressed Bayer DNG without third-party packages."""
import struct
import sys
from pathlib import Path

def make_dng(path):
    width=height=512
    entries=[]
    def tag(number,kind,values):
        if kind==2: payload=values.encode()+b'\0'; count=len(payload)
        elif kind==1: payload=bytes(values); count=len(values)
        elif kind==3: payload=struct.pack('<'+'H'*len(values),*values);count=len(values)
        elif kind==4: payload=struct.pack('<'+'I'*len(values),*values);count=len(values)
        else:
            payload=b''.join(struct.pack('<ii' if kind==10 else '<II',*value) for value in values);count=len(values)
        entries.append([number,kind,count,payload])
    for number,kind,values in [(254,4,[0]),(271,2,"Fovelle"),(272,2,"Synthetic"),(50713,3,[1,1]),(50718,5,[(1,1),(1,1)]),(50730,10,[(0,1)]),(50731,5,[(1,1)]),(50732,5,[(1,1)]),(50734,5,[(1,1)]),(50829,4,[0,0,height,width]),(256,4,[width]),(257,4,[height]),(258,3,[16]),(259,3,[1]),(262,3,[32803]),(273,4,[0]),(274,3,[1]),(277,3,[1]),(278,4,[height]),(279,4,[width*height*2]),(284,3,[1]),(33421,3,[2,2]),(33422,1,[0,1,1,2]),(50706,1,[1,4,0,0]),(50707,1,[1,1,0,0]),(50708,2,'Fovelle Synthetic Bayer'),(50710,1,[0,1,2]),(50711,3,[1]),(50714,4,[0]),(50717,4,[65535]),(50719,4,[0,0]),(50720,4,[width,height]),(50721,10,[(1,1),(0,1),(0,1),(0,1),(1,1),(0,1),(0,1),(0,1),(1,1)]),(50728,5,[(1,1)]*3),(50778,3,[21])]: tag(number,kind,values)
    entries.sort()
    offset=8+2+12*len(entries)+4
    extra=bytearray(); table=bytearray()
    pixel_offset=offset+sum((len(e[3])+1)//2*2 for e in entries if len(e[3])>4)
    for number,kind,count,payload in entries:
        if number==273: payload=struct.pack('<I',pixel_offset)
        if len(payload)<=4: value=payload.ljust(4,b'\0')
        else:
            value=struct.pack('<I',offset+len(extra));extra.extend(payload);extra.extend(b'\0'*(len(payload)%2))
        table.extend(struct.pack('<HHI',number,kind,count)+value)
    pixels=b''.join(struct.pack('<H',2048+int(60000*((x+2*y)%width)/(width-1))) for y in range(height) for x in range(width))
    Path(path).write_bytes(b'II*\0'+struct.pack('<I',8)+struct.pack('<H',len(entries))+table+b'\0'*4+extra+pixels)
if __name__=='__main__': make_dng(sys.argv[1])
