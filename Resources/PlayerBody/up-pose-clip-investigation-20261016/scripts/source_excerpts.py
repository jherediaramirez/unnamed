from pathlib import Path
import json,hashlib
OUT=Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
UE=Path(r'C:\Program Files\Epic Games\UE_5.8\Engine')
BL=Path(r'C:\dev\tools\blender-5.2.2\5.2\scripts\addons_core\io_scene_fbx')
files=[
 (UE/'Source/Runtime/Engine/Public/Animation/AnimTypes.h',[(27,28)]),
 (UE/'Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxAnimation.cpp',[(1044,1062),(1114,1140)]),
 (UE/'Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxScene.cpp',[(1098,1116)]),
 (UE/'Plugins/Interchange/Runtime/Source/Import/Private/Animation/InterchangeAnimSequenceFactory.cpp',[(531,537),(553,590),(664,710)]),
 (UE/'Plugins/Interchange/Runtime/Source/Pipelines/Private/InterchangeGenericAnimationPipeline.cpp',[(725,772)]),
 (BL/'import_fbx.py',[(2620,2640),(625,645),(680,709)]),
 (BL/'fbx_utils.py',[(1820,1832)]),
]
out=[]
for p,ranges in files:
    content=p.read_bytes();lines=content.decode('utf-8-sig').splitlines()
    out.append({'path':str(p),'sha256':hashlib.sha256(content).hexdigest(),'excerpts':[{'start':a,'end':b,'text':'\n'.join(f'{i}: {lines[i-1]}' for i in range(a,b+1))} for a,b in ranges]})
(OUT/'evidence/source_excerpts.json').write_text(json.dumps(out,indent=2))
print('Saved',len(out),'version-specific source files and excerpts')
