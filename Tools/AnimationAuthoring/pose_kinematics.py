"""스켈레톤 레퍼런스 포즈로 본의 컴포넌트 공간 위치를 계산한다 (에디터 왕복 없음).

CreateBoneAnimation의 reference_offset 모드는 로컬 회전을 `레퍼런스 * 오프셋`으로 합성한다.
어느 축이 어느 방향으로 도는지 추측하지 않으려면 오프셋을 넣은 포즈를 미리 계산해 보는 편이 빠르다.

사용 예:
    from pose_kinematics import Skeleton
    skeleton = Skeleton.from_mcp(client, '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
    print(skeleton.bone_location('hand_r', {'upperarm_r': (0, 0, -40)}))

좌표계는 이 스켈레톤의 컴포넌트 공간이다(실측): -X 가 캐릭터 오른쪽, +Y 가 앞, +Z 가 위.
단위는 센티미터, 회전은 도 단위의 [pitch, yaw, roll]이다.
"""
import math


def quaternion_from_euler(pitch, yaw, roll):
    """언리얼 FRotator(pitch, yaw, roll)를 쿼터니언 (x, y, z, w)로 바꾼다."""
    half_pitch = math.radians(pitch) * 0.5
    half_yaw = math.radians(yaw) * 0.5
    half_roll = math.radians(roll) * 0.5
    sp, cp = math.sin(half_pitch), math.cos(half_pitch)
    sy, cy = math.sin(half_yaw), math.cos(half_yaw)
    sr, cr = math.sin(half_roll), math.cos(half_roll)
    return (
        cr * sp * sy - sr * cp * cy,
        -cr * sp * cy - sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        cr * cp * cy + sr * sp * sy,
    )


def euler_from_quaternion(q):
    """쿼터니언 (x, y, z, w) 를 언리얼 FRotator(pitch, yaw, roll) 도 단위로 바꾼다.

    엔진 `FQuat::Rotator()`(UnrealMath.cpp:643-676) 와 같은 규약이다. 특이점에서는
    엔진처럼 roll 을 0 으로 두고 yaw 로 몰아준다.
    """
    x, y, z, w = q
    singularity = z * x - w * y
    yaw_y = 2.0 * (w * z + x * y)
    yaw_x = 1.0 - 2.0 * (y * y + z * z)
    threshold = 0.4999995
    if singularity < -threshold:
        pitch = -90.0
        yaw = normalize_axis(-2.0 * math.degrees(math.atan2(x, w)))
        roll = 0.0
    elif singularity > threshold:
        pitch = 90.0
        yaw = normalize_axis(2.0 * math.degrees(math.atan2(x, w)))
        roll = 0.0
    else:
        pitch = math.degrees(math.asin(max(-1.0, min(1.0, 2.0 * singularity))))
        yaw = math.degrees(math.atan2(yaw_y, yaw_x))
        roll = math.degrees(math.atan2(-2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y)))
    return (pitch, yaw, roll)


def normalize_axis(angle):
    angle = math.fmod(angle, 360.0)
    if angle > 180.0:
        angle -= 360.0
    elif angle < -180.0:
        angle += 360.0
    return angle


def quaternion_multiply(a, b):
    """언리얼 FQuat 연산자 * 와 같은 순서: a * b 는 b 를 먼저 적용한 뒤 a 를 적용한다."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def quaternion_rotate(q, v):
    """쿼터니언으로 벡터를 회전한다."""
    qx, qy, qz, qw = q
    vx, vy, vz = v
    tx = 2.0 * (qy * vz - qz * vy)
    ty = 2.0 * (qz * vx - qx * vz)
    tz = 2.0 * (qx * vy - qy * vx)
    return (
        vx + qw * tx + qy * tz - qz * ty,
        vy + qw * ty + qz * tx - qx * tz,
        vz + qw * tz + qx * ty - qy * tx,
    )


class Skeleton:
    """레퍼런스 포즈를 담고 오프셋을 적용한 본 위치를 계산한다."""

    def __init__(self, bones):
        self.order = [bone['name'] for bone in bones]
        self.parent = {bone['name']: bone['parent'] for bone in bones}
        self.translation = {bone['name']: tuple(bone['translation']) for bone in bones}
        self.rotation = {}
        for bone in bones:
            x, y, z, w = bone['rotation_quaternion']
            self.rotation[bone['name']] = (x, y, z, w)

    @classmethod
    def from_mcp(cls, client, skeletal_mesh):
        result = client.call('.TDAnimationAuthoringTools.InspectSkeleton', SkeletalMeshPath=skeletal_mesh)
        if not result.get('success'):
            raise RuntimeError(result.get('error', 'InspectSkeleton failed'))
        return cls(result['bones'])

    def component_transforms(self, offsets=None, translations=None, base_locals=None):
        """{본 이름: (위치, 회전)} 을 컴포넌트 공간으로 반환한다.

        offsets 는 {본: (pitch, yaw, roll)} 회전 오프셋, translations 는 {본: (x, y, z)} 로
        기준 위치에 더할 이동량이다. 루트 모션은 root 본에 translations 를 주어 만든다.

        base_locals 는 {본: (위치3, 쿼터니언4)} 로, 그 본의 기준 로컬 트랜스폼을 레퍼런스
        포즈 대신 쓴다. 기존 애니메이션의 한 프레임을 바탕에 깔고 팔만 다시 풀 때 쓴다.
        """
        offsets = offsets or {}
        translations = translations or {}
        base_locals = base_locals or {}
        transforms = {}
        for name in self.order:
            base = base_locals.get(name)
            local_rotation = base[1] if base else self.rotation[name]
            if name in offsets:
                local_rotation = quaternion_multiply(local_rotation, quaternion_from_euler(*offsets[name]))
            local_translation = base[0] if base else self.translation[name]
            if name in translations:
                shift = translations[name]
                local_translation = tuple(local_translation[i] + shift[i] for i in range(3))
            parent = self.parent.get(name)
            if parent:
                parent_location, parent_rotation = transforms[parent]
                rotated = quaternion_rotate(parent_rotation, local_translation)
                location = tuple(parent_location[i] + rotated[i] for i in range(3))
                rotation = quaternion_multiply(parent_rotation, local_rotation)
            else:
                location, rotation = local_translation, local_rotation
            transforms[name] = (location, rotation)
        return transforms

    def bone_location(self, bone, offsets=None, translations=None, base_locals=None):
        return self.component_transforms(offsets, translations, base_locals)[bone][0]

    def local_rotation(self, bone, offset=None, base_locals=None):
        """오프셋을 합성한 최종 로컬 회전 쿼터니언. local_absolute 모드로 쓸 값이다."""
        base = (base_locals or {}).get(bone)
        rotation = base[1] if base else self.rotation[bone]
        if offset:
            rotation = quaternion_multiply(rotation, quaternion_from_euler(*offset))
        return rotation

    def describe(self, bone, offsets=None, reference=None):
        """레퍼런스 포즈 대비 이동량을 사람이 읽을 수 있게 돌려준다."""
        location = self.bone_location(bone, offsets)
        base = reference if reference is not None else self.bone_location(bone)
        delta = tuple(location[i] - base[i] for i in range(3))
        return '%s at [%7.1f %7.1f %7.1f]  delta [%+7.1f %+7.1f %+7.1f]  (앞뒤 / 좌우 / 위아래)' % (
            bone, location[0], location[1], location[2], delta[0], delta[1], delta[2])


# 관절 가동 범위 (도). 0=pitch, 1=yaw, 2=roll
DEFAULT_LIMITS = {
    'clavicle_r': {0: (-18.0, 20.0), 1: (-18.0, 18.0)},
    'upperarm_r': {0: (-60.0, 95.0), 1: (-45.0, 80.0)},
    'lowerarm_r': {1: (-35.0, 95.0)},
    # 다리: 고관절 yaw 가 앞뒤(음수=앞), pitch 가 벌림. 무릎 yaw 는 굽힘 전용이라 음수를 막는다.
    'thigh_r': {0: (-25.0, 25.0), 1: (-70.0, 45.0)},
    'thigh_l': {0: (-25.0, 25.0), 1: (-45.0, 70.0)},
    'calf_r': {1: (0.0, 110.0)},
    'calf_l': {1: (-110.0, 0.0)},
}

ARM_AXES = {'clavicle_r': [0, 1], 'upperarm_r': [0, 1], 'lowerarm_r': [1]}
LEG_R_AXES = {'thigh_r': [0, 1], 'calf_r': [1]}
LEG_L_AXES = {'thigh_l': [0, 1], 'calf_l': [1]}


def solve_chain(skeleton, end_bone, target, axes, base_offsets=None, translations=None,
                preferred=None, limits=None, stiffness=0.06, iterations=300, base_locals=None):
    """end_bone 이 target 에 가도록 관절 오프셋을 역으로 푼다 (좌표 하강법).

    target 은 컴포넌트 공간 (x, y, z) 다. axes 는 {본: [축 인덱스]} 이고 0=pitch, 1=yaw,
    2=roll 이다. base_offsets 의 다른 본(척추 등)과 translations(루트 이동)는 고정된다.

    limits 로 관절 가동 범위를 제한하고 preferred 로 선호 각도를 준다. 목표만 맞추면
    쇄골이 50도 꺾이는 식의 해가 나오므로 선호 각도에서 벗어난 만큼 stiffness 를 곱해
    벌점을 준다. 반환값은 (오프셋 딕셔너리, 위치 오차 cm).
    """
    limits = limits or DEFAULT_LIMITS
    preferred = preferred or {}
    offsets = {name: list(value) for name, value in (base_offsets or {}).items()}
    for name in axes:
        offsets.setdefault(name, [0.0, 0.0, 0.0])

    def clamp(name, axis, value):
        low, high = limits.get(name, {}).get(axis, (-170.0, 170.0))
        return max(low, min(high, value))

    for name in axes:
        for axis in range(3):
            offsets[name][axis] = clamp(name, axis, offsets[name][axis])

    def distance(current):
        location = skeleton.component_transforms(
            {k: tuple(v) for k, v in current.items()}, translations, base_locals)[end_bone][0]
        return sum((location[i] - target[i]) ** 2 for i in range(3)) ** 0.5

    def cost(current):
        penalty = 0.0
        for name, wanted in preferred.items():
            if name not in current:
                continue
            for axis, value in enumerate(wanted):
                if value is not None:
                    penalty += abs(current[name][axis] - value)
        return distance(current) + stiffness * penalty

    best = cost(offsets)
    step = 24.0
    for _ in range(iterations):
        improved = False
        for name, indices in axes.items():
            for axis in indices:
                for delta in (step, -step):
                    original = offsets[name][axis]
                    candidate = clamp(name, axis, original + delta)
                    if candidate == original:
                        continue
                    offsets[name][axis] = candidate
                    score = cost(offsets)
                    if score < best - 1e-4:
                        best, improved = score, True
                        break
                    offsets[name][axis] = original
        if not improved:
            step *= 0.5
            if step < 0.05:
                break
    return {k: tuple(round(v, 1) for v in val) for k, val in offsets.items()}, distance(offsets)


def solve_arm(skeleton, target, base_offsets=None, axes=None, preferred=None,
              limits=None, stiffness=0.06, iterations=300, translations=None, base_locals=None):
    """오른손을 target 으로 보내는 팔 오프셋을 푼다 (solve_chain 의 팔 전용 래퍼)."""
    return solve_chain(skeleton, 'hand_r', target, axes or ARM_AXES, base_offsets,
                       translations, preferred, limits, stiffness, iterations, base_locals)
