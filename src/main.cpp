#include "util/memory.h"
#include <cstddef>
#include <Windows.h>

using enum hkpCharacterState::StateType;

struct CharacterMoveParams {
	// this struct alignment pisses me off
	float multiplier;
	AlignedVector4 forward;
	AlignedVector4 up;
	AlignedVector4 groundNormal;
	AlignedVector4 velocity;
	AlignedVector4 input;
	float maxSpeed;
	AlignedVector4 surfaceVelocity;
};

namespace ini {
constexpr auto fFriction = 5.f;
constexpr auto fAcceleration = 6.f;
constexpr auto fAirAcceleration = 1.f;
constexpr auto fStopSpeed = 8.f;
constexpr auto fAirSpeed = 1.f;
}

static void ApplyFriction(
	const CharacterMoveParams &move,
	AlignedVector4 *velocity,
	float deltaTime)
{
	const auto speed = ((NiVector3&)*velocity).Length();
	const auto scaleSpeed = std::max(speed, ini::fStopSpeed);
	const auto friction = ini::fFriction * scaleSpeed * move.groundNormal.z * deltaTime;

	if (friction >= speed)
		*velocity = AlignedVector4(0, 0, 0, 0);
	else
		*velocity *= 1.f - friction / speed;
}

static void ApplyAcceleration(
	const CharacterMoveParams &move,
	AlignedVector4 *velocity,
	UInt32 state,
	const NiVector3 &moveVector,
	float moveLength,
	float deltaTime)
{
	const auto inAir = state == kState_InAir;
	const auto speed = ((NiVector3&)*velocity).DotProduct(moveVector);
	const auto maxSpeed = inAir ? moveLength * ini::fAirSpeed : moveLength;
	const auto speedCap = std::max(maxSpeed, ((NiVector3&)*velocity).Length());

	if (speed >= maxSpeed)
		return;

	const auto accelMultiplier = inAir ? ini::fAirAcceleration : ini::fAcceleration;
	const auto accel = accelMultiplier * moveLength * move.groundNormal.z * deltaTime;
	*velocity += moveVector * std::min(accel, maxSpeed - speed);

	if (const auto newLength = ((NiVector3&)*velocity).Length(); newLength > speedCap)
		*velocity *= speedCap / newLength;
}

static AlignedVector4 GetMoveVector(const CharacterMoveParams &move)
{
	const auto &input = move.input;
	const auto &forward = move.forward;
	const auto &up = move.up;
	const auto right = AlignedVector4(((NiVector3&)forward).CrossProduct(up));
	const auto moveVectorRaw = forward * -input.x + right * input.y + up * input.z;
	const auto moveVector = NiVector3(moveVectorRaw).Normalize();
	const auto &normal = move.groundNormal;

	if (normal.z <= 1e-4f || normal.z >= 1.f - 1e-4f)
		return {moveVector};

	const auto dot = moveVector.DotProduct(normal);
	return {NiVector3(moveVector.x, moveVector.y, -dot / normal.z).Normalize()};
}

static void UpdateVelocity(
	const CharacterMoveParams &move,
	AlignedVector4 *velocity,
	UInt32 state,
	float deltaTime)
{
	if (state != kState_InAir)
		ApplyFriction(move, velocity, deltaTime);

	if (const auto moveLength = ((NiVector3&)move.input).Length(); moveLength >= 1e-4f) {
		const auto moveVector = GetMoveVector(move);
		ApplyAcceleration(move, velocity, state, moveVector, moveLength, deltaTime);
	}
}

static bool shouldUsePhysics(bhkCharacterController *charCtrl)
{
	return charCtrl == PlayerCharacter::GetSingleton()->GetCharacterController()
	    && VATSCameraData::Get()->mode == 0;
}

static void hook_MoveCharacter(
	bhkCharacterController *charCtrl,
	CharacterMoveParams *move,
	AlignedVector4 *velocity)
{
	if (!shouldUsePhysics(charCtrl)) {
		// call original
		CdeclCall(0xD6AEF0, move, velocity);
		return;
	}

	const auto state = charCtrl->chrContext.hkState;
	const auto deltaTime = charCtrl->stepInfo.deltaTime;

	*velocity -= move->surfaceVelocity.PS();
	UpdateVelocity(*move, velocity, state, deltaTime);
	*velocity += move->surfaceVelocity.PS();

	// Prevent ground state from restoring Z velocity
	if (state == kState_OnGround)
		move->velocity.z = velocity->z;
}

static __declspec(naked) void hook_MoveCharacter_wrapper()
{
	__asm {
		push [esp+8]
		push [esp+8]
		push esi
		call hook_MoveCharacter
		add esp, 12
		ret
	}
}

static void patch_call_rel32(const uintptr_t addr, const void *dest)
{
	DWORD old_protect;
	VirtualProtect((void*)addr, 5, PAGE_EXECUTE_READWRITE, &old_protect);
	*(char*)addr = '\xE8'; // CALL opcode
	*(int32_t*)(addr + 1) = make_rel32((void*)addr, dest, 5);
	VirtualProtect((void*)addr, 5, old_protect, &old_protect);
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "Player Physics";
	info->version = 1;
	return !nvse->isEditor;
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Load(NVSEInterface *nvse)
{
	patch_call_rel32(0xCD414D, hook_MoveCharacter_wrapper);
	patch_call_rel32(0xCD45D0, hook_MoveCharacter_wrapper);
	patch_call_rel32(0xCD4A2A, hook_MoveCharacter_wrapper);
	return true;
}