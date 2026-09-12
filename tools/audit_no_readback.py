from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
forbidden={
 'BitBlt':'desktop/GDI capture', 'PrintWindow':'window screenshot capture',
 'Windows.Graphics.Capture':'desktop capture API', 'D3D11_USAGE_STAGING':'D3D11 CPU staging frame path',
 'D3D12_HEAP_TYPE_READBACK':'D3D12 CPU readback heap', 'GetDC(':'GDI surface extraction'
}
fail=[]
for p in list((root/'src').rglob('*'))+list((root/'shaders').rglob('*')):
 if p.suffix.lower() not in {'.cpp','.hpp','.h','.hlsl','.c'}: continue
 text=p.read_text(errors='ignore')
 for token,why in forbidden.items():
  if token in text: fail.append((p.relative_to(root),token,why))
if fail:
 for x in fail: print(f'FAIL {x[0]}: {x[1]} ({x[2]})')
 sys.exit(1)
print('PASS: no screenshot/staging/readback API tokens found in frame-path sources')
