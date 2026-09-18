"""Blender 안에서 실행: 그립 개정 전에 현재 문서를 스냅샷으로 보존하고 빈 씬으로 초기화한다.
실행: Blender MCP execute_blender_code로 본문을 전달(`python Tools/BlenderMCP/call_tool.py --code …`)
출력: Saved/BlenderAnimation/BeforeGripRevision_<YYYYMMDD_HHMMSS>.blend
상태: 현행
"""
import json
from datetime import datetime
from pathlib import Path

import bpy

directory = Path('C:/Project/TDGame/Saved/BlenderAnimation')
snapshot = directory / ('BeforeGripRevision_' + datetime.now().strftime('%Y%m%d_%H%M%S') + '.blend')
bpy.ops.wm.save_as_mainfile(filepath=str(snapshot), copy=True, check_existing=True)
print(json.dumps({'preserved_session': str(snapshot), 'original_document': bpy.data.filepath}))
bpy.ops.wm.read_factory_settings(use_empty=True)
