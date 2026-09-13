from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
forbidden={
 'BitBlt':'desktop/GDI capture', 'PrintWindow':'window screenshot capture',
 'Windows.Graphics.Capture':'desktop capture API', 'D3D11_USAGE_STAGING':'D3D11 CPU staging frame path',
 'D3D12_HEAP_TYPE_READBACK':'D3D12 CPU readback heap', 'GetDC(':'GDI surface extraction'
}
# The direct Feature-18 profiler resolves two 64-bit D3D12 timestamp queries per
# frame slot. This reads timing metadata after the completion fence; it never
# maps color/depth/motion textures or copies frame pixels to the CPU. Keep the
# exception line-local so any other D3D12 readback heap still fails this audit.
timestamp_marker='UDLSS_TIMESTAMP_QUERY_READBACK'
fail=[]
for p in list((root/'src').rglob('*'))+list((root/'shaders').rglob('*')):
 if p.suffix.lower() not in {'.cpp','.hpp','.h','.hlsl','.c'}: continue
 text=p.read_text(errors='ignore')
 for line_no,line in enumerate(text.splitlines(),1):
  for token,why in forbidden.items():
   if token not in line: continue
   if token=='D3D12_HEAP_TYPE_READBACK' and timestamp_marker in line: continue
   fail.append((p.relative_to(root),line_no,token,why))
if fail:
 for x in fail: print(f'FAIL {x[0]}:{x[1]}: {x[2]} ({x[3]})')
 sys.exit(1)
print('PASS: no screenshot/staging/frame-readback API tokens found; timestamp metadata readback only')
