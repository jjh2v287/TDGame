import json

import bpy

server = bpy.types.blendermcp_server
probe_name = "TD_MCP_SmokeProbe"
if bpy.data.objects.get(probe_name):
    raise RuntimeError("The smoke probe name is already in use.")
probe = bpy.data.objects.new(probe_name, None)
bpy.context.collection.objects.link(probe)
probe.location.x = 0.0
probe.keyframe_insert(data_path="location", frame=1)
probe.location.x = 0.25
probe.keyframe_insert(data_path="location", frame=2)
bpy.context.scene.frame_set(2)
assert abs(probe.location.x - 0.25) < 1e-6
action = probe.animation_data.action
bpy.data.objects.remove(probe, do_unlink=True)
bpy.data.actions.remove(action)
bpy.context.scene.frame_set(1)
print(json.dumps({
    "blender_version": bpy.app.version_string,
    "project": bpy.context.scene.get("TD_MCP_PROJECT"),
    "host": server.host,
    "port": server.port,
    "telemetry_consent": server.get_telemetry_consent()["consent"],
    "integrations": {name: getattr(bpy.context.scene, "blendermcp_use_" + name) for name in ("polyhaven", "hyper3d", "sketchfab", "polypizza", "hunyuan3d")},
    "animated_probe_created_evaluated_removed": True,
}))
