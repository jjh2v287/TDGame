# File: Tools/Animation/kimodo_ue_processor.py
import os
import sys
import subprocess
import argparse
import tempfile
from pathlib import Path

PYTHON_KIMODO = r"C:\Tools\kimodo\.venv\Scripts\python.exe"
BLENDER_EXE = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
UE_CMD_EXE = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
UPROJECT = r"C:\Project\TDGame\TDGame.uproject"


def generate_kimodo_motion(prompt: str, duration: float, steps: int, output_stem: str) -> str:
    """Kimodo 3D 모션 생성 모델을 구동하여 BVH 애니메이션 추출"""
    env = os.environ.copy()
    env["TEXT_ENCODER_DEVICE"] = "cpu"  # 6GB VRAM 환경 최적화
    
    cmd = [
        PYTHON_KIMODO,
        "-m", "kimodo.scripts.generate",
        prompt,
        "--duration", str(duration),
        "--diffusion_steps", str(steps),
        "--bvh",
        "--bvh_standard_tpose",
        "--no-postprocess",
        "--output", output_stem
    ]
    
    print(f"[Kimodo] Running motion generation: '{prompt}'...")
    res = subprocess.run(cmd, cwd=r"C:\Tools\kimodo", env=env, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[Kimodo Error] {res.stderr}")
        raise RuntimeError("Kimodo generation failed.")
    
    bvh_path = f"{output_stem}.bvh"
    if not os.path.exists(bvh_path):
        raise FileNotFoundError(f"Expected BVH not found: {bvh_path}")
        
    print(f"[Kimodo] BVH generated successfully: {bvh_path}")
    return bvh_path


def convert_bvh_to_fbx(bvh_path: str, fbx_path: str) -> str:
    """Blender Headless(bpy)를 통해 BVH를 언리얼 표준 호환 애니메이션 FBX로 변환"""
    blender_script = f'''import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_anim.bvh(filepath=r"{bvh_path}", update_scene_fps=True, update_scene_duration=True)
bpy.ops.export_scene.fbx(
    filepath=r"{fbx_path}",
    bake_anim=True,
    bake_anim_use_all_bones=True,
    bake_anim_step=1.0,
    add_leaf_bones=False
)
print("BVH to FBX conversion complete.")
'''
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False, encoding="utf-8") as f:
        script_file = f.name
        f.write(blender_script)
        
    try:
        cmd = [BLENDER_EXE, "-b", "--python", script_file]
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        print(f"[Blender] Converted to FBX: {fbx_path}")
        return fbx_path
    finally:
        if os.path.exists(script_file):
            os.remove(script_file)


def import_fbx_to_unreal(fbx_path: str, destination_path: str, asset_name: str, skeleton_path: str) -> bool:
    """UnrealEditor-Cmd를 구동하여 FBX를 UAnimSequence로 임포트"""
    ue_script = f'''import unreal
task = unreal.AssetImportTask()
task.filename = r"{fbx_path}"
task.destination_path = "{destination_path}"
task.destination_name = "{asset_name}"
task.replace_existing = True
task.automated = True
task.save = True

options = unreal.FbxImportUI()
options.import_mesh = False
options.import_animations = True
options.skeleton = unreal.load_asset("{skeleton_path}")

task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
print(">>> IMPORTED_PATHS:", task.imported_object_paths)
'''
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False, encoding="utf-8") as f:
        script_file = f.name
        f.write(ue_script)
        
    try:
        cmd = [
            UE_CMD_EXE,
            UPROJECT,
            "-run=pythonscript",
            f"-script={script_file}",
            "-stdout",
            "-nosplash",
            "-nullrhi"
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        success = "IMPORTED_PATHS: ['" in res.stdout
        print(f"[UE Import] Result: {success} -> {destination_path}/{asset_name}")
        return success
    finally:
        if os.path.exists(script_file):
            os.remove(script_file)


def run_full_pipeline(prompt: str, asset_name: str, duration: float = 1.5, steps: int = 50):
    output_dir = Path(r"C:\Project\TDGame\Saved\TempAnim").resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    
    stem = str(output_dir / asset_name)
    bvh_file = generate_kimodo_motion(prompt, duration, steps, stem)
    fbx_file = str(output_dir / f"{asset_name}.fbx")
    convert_bvh_to_fbx(bvh_file, fbx_file)
    
    dest_path = "/Game/Characters/Mannequins/Anims/Sword"
    skeleton = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
    import_fbx_to_unreal(fbx_file, dest_path, asset_name, skeleton)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--prompt", type=str, required=True)
    parser.add_argument("--name", type=str, required=True)
    parser.add_argument("--duration", type=float, default=1.5)
    parser.add_argument("--steps", type=int, default=50)
    args = parser.parse_args()
    
    run_full_pipeline(args.prompt, args.name, args.duration, args.steps)