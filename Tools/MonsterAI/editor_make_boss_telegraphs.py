"""실행 환경: TDGame 언리얼 에디터 안(PythonScriptPlugin 원격 실행). 읽기 전용 /Engine/BasicShapes/Plane 과 기존 /Game/MonsterAI/Materials/M_TD_Telegraph 가 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/MonsterAI/editor_make_boss_telegraphs.py  (run_in_editor.py 에는 -f 옵션이 없고 파일 경로를 바로 받는다. 인수에 /Game 경로가 없어 어느 셸이든 된다)
출력: /Game/MonsterAI/Materials 의 M_TD_TelegraphCone·M_TD_TelegraphBand 재질 2개, MI_TD_Telegraph_{Unblockable,Safe}·MI_TD_TelegraphCone_{Danger,Unblockable}_{20,30,35,45,60,90}·MI_TD_TelegraphBand_{Danger,Unblockable} 인스턴스 16개, Plane UV 축 계산값·파라미터 확인값·남은 dirty 패키지 줄, 결과 JSON 한 줄(TD_TELEGRAPH_RESULT)
상태: 신규(M3-35, 아직 실행 전). 이미 있는 에셋은 건너뛰고 덮어쓰지 않는다. 새로 만든 에셋만 저장한다.
"""
import json
import math

import unreal


FOLDER = "/Game/MonsterAI/Materials"
DISK_MATERIAL_PATH = FOLDER + "/M_TD_Telegraph"
CONE_MATERIAL_NAME = "M_TD_TelegraphCone"
BAND_MATERIAL_NAME = "M_TD_TelegraphBand"
PLANE_MESH_PATH = "/Engine/BasicShapes/Plane"
CONE_HALF_ANGLES = (20, 30, 35, 45, 60, 90)
COLORS = {
    "Danger": (1.0, 0.1, 0.05),
    "Unblockable": (0.62, 0.18, 1.0),
    "Safe": (0.1, 0.9, 0.85),
}
DEFAULT_OPACITY = 0.45
SAFE_OPACITY = 0.35
RADIAL_RING_EXPONENT = 6.0
RADIAL_FILL_MINIMUM = 0.3
RADIAL_EDGE_SHARPNESS = 40.0
ANGLE_SOFT_DEGREES = 1.5
ANGLE_GLOW_DEGREES = 3.0
ANGLE_GLOW_STRENGTH = 0.9
BAND_EDGE_WIDTH = 0.06
BAND_EDGE_SOFTNESS = 0.01
BAND_FILL = 0.3
RADIANS_TO_DEGREES = 180.0 / math.pi
UV_FIT_TOLERANCE = 1e-3
COLOR_TOLERANCE = 1e-4


def solve_gradient(points, values):
    sxx = sum(x * x for x, _ in points)
    sxy = sum(x * y for x, y in points)
    syy = sum(y * y for _, y in points)
    sxv = sum(x * v for (x, _), v in zip(points, values))
    syv = sum(y * v for (_, y), v in zip(points, values))
    determinant = sxx * syy - sxy * sxy
    if abs(determinant) < 1e-9:
        raise RuntimeError("Plane 정점이 한 직선 위에 있어 UV 기울기를 풀 수 없습니다")
    return (sxv * syy - syv * sxy) / determinant, (sxx * syv - sxy * sxv) / determinant


def describe_uv_axis(du, dv):
    if abs(du) >= abs(dv):
        return "U" + ("+" if du > 0 else "-")
    return "V" + ("+" if dv > 0 else "-")


def read_plane_uv_axes():
    mesh = unreal.load_asset(PLANE_MESH_PATH)
    if mesh is None:
        raise RuntimeError("Plane 메시를 불러오지 못했습니다: " + PLANE_MESH_PATH)
    description = mesh.get_static_mesh_description(0)
    samples = []
    for index in range(description.get_vertex_instance_count()):
        instance_id = unreal.VertexInstanceID()
        instance_id.set_editor_property("id_value", index)
        position = description.get_vertex_position(description.get_vertex_instance_vertex(instance_id))
        uv = description.get_vertex_instance_uv(instance_id, 0)
        samples.append((position.x, position.y, uv.x, uv.y))
    if len(samples) < 4:
        raise RuntimeError("Plane 정점 인스턴스가 4개 미만입니다: %d" % len(samples))

    mean_x = sum(s[0] for s in samples) / len(samples)
    mean_y = sum(s[1] for s in samples) / len(samples)
    mean_u = sum(s[2] for s in samples) / len(samples)
    mean_v = sum(s[3] for s in samples) / len(samples)
    points = [(s[0] - mean_x, s[1] - mean_y) for s in samples]
    du_dx, du_dy = solve_gradient(points, [s[2] - mean_u for s in samples])
    dv_dx, dv_dy = solve_gradient(points, [s[3] - mean_v for s in samples])
    jacobian = du_dx * dv_dy - du_dy * dv_dx
    if abs(jacobian) < 1e-9:
        raise RuntimeError("Plane UV 사상이 퇴화했습니다")
    local_x = (dv_dy / jacobian, -du_dy / jacobian)
    local_y = (-dv_dx / jacobian, du_dx / jacobian)
    origin_u = mean_u - du_dx * mean_x - du_dy * mean_y
    origin_v = mean_v - dv_dx * mean_x - dv_dy * mean_y
    if abs(origin_u - 0.5) > UV_FIT_TOLERANCE or abs(origin_v - 0.5) > UV_FIT_TOLERANCE:
        raise RuntimeError("Plane 중심이 UV (0.5, 0.5) 가 아닙니다: (%.4f, %.4f)" % (origin_u, origin_v))

    worst_fit = 0.0
    worst_recovery = 0.0
    for (x, y), sample in zip(points, samples):
        predicted_u = du_dx * x + du_dy * y
        predicted_v = dv_dx * x + dv_dy * y
        worst_fit = max(worst_fit, abs(sample[2] - mean_u - predicted_u), abs(sample[3] - mean_v - predicted_v))
        offset_u = sample[2] - 0.5
        offset_v = sample[3] - 0.5
        recovered_x = local_x[0] * offset_u + local_x[1] * offset_v
        recovered_y = local_y[0] * offset_u + local_y[1] * offset_v
        worst_recovery = max(worst_recovery, abs(recovered_x - sample[0]), abs(recovered_y - sample[1]))
    if worst_fit > UV_FIT_TOLERANCE:
        raise RuntimeError("Plane UV 가 평면 위 선형이 아닙니다(최대 오차 %.5f)" % worst_fit)
    if worst_recovery > 0.1:
        raise RuntimeError("Plane UV 역변환이 정점 위치를 되찾지 못합니다(최대 오차 %.4f cm)" % worst_recovery)
    result = {
        "vertex_instances": len(samples),
        "extent_x_cm": round(max(s[0] for s in samples) - min(s[0] for s in samples), 3),
        "extent_y_cm": round(max(s[1] for s in samples) - min(s[1] for s in samples), 3),
        "uv_at_origin": [round(origin_u, 5), round(origin_v, 5)],
        "local_x_in_uv": describe_uv_axis(du_dx, dv_dx),
        "local_y_in_uv": describe_uv_axis(du_dy, dv_dy),
        "du_dx": round(du_dx, 6),
        "du_dy": round(du_dy, 6),
        "dv_dx": round(dv_dx, 6),
        "dv_dy": round(dv_dy, 6),
        "local_x_coefficients": [round(local_x[0], 4), round(local_x[1], 4)],
        "local_y_coefficients": [round(local_y[0], 4), round(local_y[1], 4)],
        "worst_fit": round(worst_fit, 6),
        "worst_recovery_cm": round(worst_recovery, 4),
    }
    print("PLANE_UV_AXES", result)
    return result, local_x, local_y


def make_graph(material):
    editing = unreal.MaterialEditingLibrary

    def node(expression_class, x, y, **properties):
        expression = editing.create_material_expression(material, expression_class, x, y)
        if expression is None:
            raise RuntimeError("노드 생성 실패: " + expression_class.__name__)
        for key, value in properties.items():
            expression.set_editor_property(key, value)
        return expression

    def link(source, target, target_input=""):
        if not editing.connect_material_expressions(source, "", target, target_input):
            raise RuntimeError("노드 연결 실패: %s -> %s.%s" % (source.get_name(), target.get_name(), target_input))

    return node, link


def finish_material(material, node, link, color_default, opacity_expression):
    editing = unreal.MaterialEditingLibrary
    color_parameter = node(unreal.MaterialExpressionVectorParameter, -400, -240, parameter_name="Color",
                           default_value=unreal.LinearColor(color_default[0], color_default[1], color_default[2], 1.0))
    if not editing.connect_material_property(color_parameter, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("방출색 연결 실패")
    if not editing.connect_material_property(opacity_expression, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("불투명도 연결 실패")
    print("RECOMPILE_RESULT", material.get_name(), editing.recompile_material(material))
    statistics = editing.get_statistics(material)
    print("STATS", material.get_name(), {key: statistics.get_editor_property(key) for key in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")})
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("재질 저장 실패: " + material.get_name())


def create_translucent_material(name):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("재질 생성 실패: " + name)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    return material


def build_cone_material(local_x, local_y):
    material = create_translucent_material(CONE_MATERIAL_NAME)
    node, link = make_graph(material)

    uv = node(unreal.MaterialExpressionTextureCoordinate, -2400, 0)
    center = node(unreal.MaterialExpressionConstant2Vector, -2400, 160, r=0.5, g=0.5)
    distance = node(unreal.MaterialExpressionDistance, -2200, 60)
    radius = node(unreal.MaterialExpressionMultiply, -2040, 60, const_b=2.0)
    ring_curve = node(unreal.MaterialExpressionPower, -1880, 0, const_exponent=RADIAL_RING_EXPONENT)
    fill_to_ring = node(unreal.MaterialExpressionLinearInterpolate, -1720, 0, const_a=RADIAL_FILL_MINIMUM, const_b=1.0)
    inside = node(unreal.MaterialExpressionOneMinus, -1880, 180)
    edge_sharpness = node(unreal.MaterialExpressionMultiply, -1720, 180, const_b=RADIAL_EDGE_SHARPNESS)
    edge_mask = node(unreal.MaterialExpressionSaturate, -1560, 180)
    link(uv, distance, "A")
    link(center, distance, "B")
    link(distance, radius, "A")
    link(radius, ring_curve, "Base")
    link(ring_curve, fill_to_ring, "Alpha")
    link(radius, inside, "")
    link(inside, edge_sharpness, "A")
    link(edge_sharpness, edge_mask, "")

    offset = node(unreal.MaterialExpressionSubtract, -2200, 420, const_b=0.5)
    local_x_axis = node(unreal.MaterialExpressionConstant2Vector, -2200, 560, r=local_x[0], g=local_x[1])
    local_y_axis = node(unreal.MaterialExpressionConstant2Vector, -2200, 700, r=local_y[0], g=local_y[1])
    along = node(unreal.MaterialExpressionDotProduct, -2040, 480)
    across = node(unreal.MaterialExpressionDotProduct, -2040, 640)
    side = node(unreal.MaterialExpressionAbs, -1880, 640)
    angle_radians = node(unreal.MaterialExpressionArctangent2, -1720, 560)
    angle_degrees = node(unreal.MaterialExpressionMultiply, -1560, 560, const_b=RADIANS_TO_DEGREES)
    link(uv, offset, "A")
    link(offset, along, "A")
    link(local_x_axis, along, "B")
    link(offset, across, "A")
    link(local_y_axis, across, "B")
    link(across, side, "")
    link(side, angle_radians, "Y")
    link(along, angle_radians, "X")
    link(angle_radians, angle_degrees, "A")

    half_angle = node(unreal.MaterialExpressionScalarParameter, -1880, 860, parameter_name="HalfAngleDegrees", default_value=45.0)
    limit = node(unreal.MaterialExpressionAdd, -1720, 860, const_b=ANGLE_SOFT_DEGREES)
    margin = node(unreal.MaterialExpressionSubtract, -1560, 780)
    margin_scaled = node(unreal.MaterialExpressionMultiply, -1400, 780, const_b=1.0 / ANGLE_SOFT_DEGREES)
    angle_mask = node(unreal.MaterialExpressionSaturate, -1240, 780)
    link(half_angle, limit, "A")
    link(limit, margin, "A")
    link(angle_degrees, margin, "B")
    link(margin, margin_scaled, "A")
    link(margin_scaled, angle_mask, "")

    glow_start = node(unreal.MaterialExpressionSubtract, -1720, 1000, const_b=ANGLE_GLOW_DEGREES)
    glow_depth = node(unreal.MaterialExpressionSubtract, -1560, 960)
    glow_scaled = node(unreal.MaterialExpressionMultiply, -1400, 960, const_b=1.0 / ANGLE_GLOW_DEGREES)
    glow = node(unreal.MaterialExpressionSaturate, -1240, 960)
    glow_strength = node(unreal.MaterialExpressionMultiply, -1080, 960, const_b=ANGLE_GLOW_STRENGTH)
    link(half_angle, glow_start, "A")
    link(angle_degrees, glow_depth, "A")
    link(glow_start, glow_depth, "B")
    link(glow_depth, glow_scaled, "A")
    link(glow_scaled, glow, "")
    link(glow, glow_strength, "A")

    profile = node(unreal.MaterialExpressionMax, -900, 80)
    radial_shape = node(unreal.MaterialExpressionMultiply, -740, 80)
    masked_shape = node(unreal.MaterialExpressionMultiply, -580, 120)
    opacity_parameter = node(unreal.MaterialExpressionScalarParameter, -580, 300, parameter_name="Opacity", default_value=DEFAULT_OPACITY)
    opacity = node(unreal.MaterialExpressionMultiply, -420, 160)
    link(fill_to_ring, profile, "A")
    link(glow_strength, profile, "B")
    link(profile, radial_shape, "A")
    link(edge_mask, radial_shape, "B")
    link(radial_shape, masked_shape, "A")
    link(angle_mask, masked_shape, "B")
    link(masked_shape, opacity, "A")
    link(opacity_parameter, opacity, "B")

    finish_material(material, node, link, COLORS["Danger"], opacity)
    return material


def build_band_material():
    material = create_translucent_material(BAND_MATERIAL_NAME)
    node, link = make_graph(material)

    uv = node(unreal.MaterialExpressionTextureCoordinate, -1800, 0)
    offset = node(unreal.MaterialExpressionSubtract, -1640, 0, const_b=0.5)
    deviation = node(unreal.MaterialExpressionAbs, -1480, 0)
    edge_gap_vector = node(unreal.MaterialExpressionSubtract, -1320, 0, const_a=0.5)
    edge_u = node(unreal.MaterialExpressionComponentMask, -1160, -80, r=True, g=False, b=False, a=False)
    edge_v = node(unreal.MaterialExpressionComponentMask, -1160, 80, r=False, g=True, b=False, a=False)
    edge_distance = node(unreal.MaterialExpressionMin, -1000, 0)
    edge_gap = node(unreal.MaterialExpressionSubtract, -840, 0, const_a=BAND_EDGE_WIDTH + BAND_EDGE_SOFTNESS)
    edge_scaled = node(unreal.MaterialExpressionMultiply, -680, 0, const_b=1.0 / BAND_EDGE_SOFTNESS)
    edge_alpha = node(unreal.MaterialExpressionSaturate, -520, 0)
    profile = node(unreal.MaterialExpressionLinearInterpolate, -360, 0, const_a=BAND_FILL, const_b=1.0)
    opacity_parameter = node(unreal.MaterialExpressionScalarParameter, -360, 180, parameter_name="Opacity", default_value=DEFAULT_OPACITY)
    opacity = node(unreal.MaterialExpressionMultiply, -200, 80)
    link(uv, offset, "A")
    link(offset, deviation, "")
    link(deviation, edge_gap_vector, "B")
    link(edge_gap_vector, edge_u, "")
    link(edge_gap_vector, edge_v, "")
    link(edge_u, edge_distance, "A")
    link(edge_v, edge_distance, "B")
    link(edge_distance, edge_gap, "B")
    link(edge_gap, edge_scaled, "A")
    link(edge_scaled, edge_alpha, "")
    link(edge_alpha, profile, "Alpha")
    link(profile, opacity, "A")
    link(opacity_parameter, opacity, "B")

    finish_material(material, node, link, COLORS["Danger"], opacity)
    return material


def require_parameters(material, vector_names, scalar_names):
    editing = unreal.MaterialEditingLibrary
    vectors = [str(name) for name in editing.get_vector_parameter_names(material)]
    scalars = [str(name) for name in editing.get_scalar_parameter_names(material)]
    for name in vector_names:
        if name not in vectors:
            raise RuntimeError("%s 에 벡터 파라미터 %s 가 없습니다(있는 값 %s)" % (material.get_name(), name, vectors))
    for name in scalar_names:
        if name not in scalars:
            raise RuntimeError("%s 에 스칼라 파라미터 %s 가 없습니다(있는 값 %s)" % (material.get_name(), name, scalars))


def load_or_build_material(name, builder, vector_names, scalar_names, created, skipped):
    path = FOLDER + "/" + name
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError("기존 에셋이 Material 이 아닙니다: " + path)
        skipped.append(path)
    else:
        material = builder()
        created.append(path)
    require_parameters(material, vector_names, scalar_names)
    return material


def create_instance(name, parent, color, scalars, created, skipped, checks):
    path = FOLDER + "/" + name
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        skipped.append(path)
        return
    editing = unreal.MaterialEditingLibrary
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError("인스턴스 생성 실패: " + path)
    editing.set_material_instance_parent(instance, parent)
    editing.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
    for key, value in scalars.items():
        editing.set_material_instance_scalar_parameter_value(instance, key, value)
    editing.update_material_instance(instance)

    read_color = editing.get_material_instance_vector_parameter_value(instance, "Color")
    if max(abs(read_color.r - color[0]), abs(read_color.g - color[1]), abs(read_color.b - color[2])) > COLOR_TOLERANCE:
        raise RuntimeError("%s 의 Color 가 설정되지 않았습니다: %s" % (name, read_color))
    read_scalars = {}
    for key, value in scalars.items():
        read_scalars[key] = editing.get_material_instance_scalar_parameter_value(instance, key)
        if abs(read_scalars[key] - value) > COLOR_TOLERANCE:
            raise RuntimeError("%s 의 %s 가 설정되지 않았습니다: %s" % (name, key, read_scalars[key]))
    if not assets.save_loaded_asset(instance, only_if_is_dirty=False):
        raise RuntimeError("인스턴스 저장 실패: " + path)
    created.append(path)
    checks.append({"name": name, "parent": parent.get_name(), "color": [round(read_color.r, 4), round(read_color.g, 4), round(read_color.b, 4)], "scalars": read_scalars})


def main():
    assets = unreal.EditorAssetLibrary
    if not assets.does_asset_exist(DISK_MATERIAL_PATH):
        raise RuntimeError("원판 재질이 없습니다: " + DISK_MATERIAL_PATH)
    if not assets.does_directory_exist(FOLDER) and not assets.make_directory(FOLDER):
        raise RuntimeError("폴더 생성 실패: " + FOLDER)

    axes, local_x, local_y = read_plane_uv_axes()
    created = []
    skipped = []
    checks = []

    disk_material = assets.load_asset(DISK_MATERIAL_PATH)
    require_parameters(disk_material, ["Color"], ["Opacity"])
    cone_material = load_or_build_material(CONE_MATERIAL_NAME, lambda: build_cone_material(local_x, local_y), ["Color"], ["Opacity", "HalfAngleDegrees"], created, skipped)
    band_material = load_or_build_material(BAND_MATERIAL_NAME, build_band_material, ["Color"], ["Opacity"], created, skipped)

    create_instance("MI_TD_Telegraph_Unblockable", disk_material, COLORS["Unblockable"], {}, created, skipped, checks)
    create_instance("MI_TD_Telegraph_Safe", disk_material, COLORS["Safe"], {"Opacity": SAFE_OPACITY}, created, skipped, checks)
    for style in ("Danger", "Unblockable"):
        for half_angle in CONE_HALF_ANGLES:
            create_instance("MI_TD_TelegraphCone_%s_%d" % (style, half_angle), cone_material, COLORS[style],
                            {"HalfAngleDegrees": float(half_angle)}, created, skipped, checks)
        create_instance("MI_TD_TelegraphBand_" + style, band_material, COLORS[style], {}, created, skipped, checks)

    for check in checks:
        print("PARAMETER_CHECK", check)
    dirty_packages = [package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    print("DIRTY_CONTENT", dirty_packages)
    print("TD_TELEGRAPH_RESULT " + json.dumps({"created": created, "skipped": skipped, "plane_uv": axes, "checks": checks, "dirty_content": dirty_packages}, ensure_ascii=False))


main()
