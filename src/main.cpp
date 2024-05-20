#include "util/memory.h"
#include <cstddef>
#include <Windows.h>

using enum hkpCharacterState::StateType;
using enum bhkCharacterListener::ListenerFlags;
using enum ActorMover::MovementFlags;

constexpr auto kHavokUnitScale = 1.f / 6.9991255f;

enum ControlState {
	kControlState_Held = 0,
	kControlState_Pressed = 1
};

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
constexpr auto fMinAccelScaleSpeed = 25.f;
constexpr auto fStopSpeed = 16.f;
constexpr auto fAirSpeed = 1.f;
constexpr auto fGravityMult = 2.f;
constexpr auto fKnockbackScale = 10.f;
}

struct {
	bool usedJumpInput = true;
	bool justLanded = false;
} g_player;

static PlayerCharacter *GetPlayer()
{
	return PlayerCharacter::GetSingleton();
}

static bool IsPlayerController(bhkCharacterController *charCtrl)
{
	return charCtrl == GetPlayer()->GetCharacterController();
}

static bool IsMovementOverrideSequence(UInt16 sequence)
{
	return CdeclCall<bool>(0x5F2670, sequence);
}

static bool ShouldUsePhysics(bhkCharacterController *charCtrl)
{
	if (!IsPlayerController(charCtrl))
		return false;

	if (VATSCameraData::Get()->mode != 0)
		return false;

	const auto *animData = GetPlayer()->GetAnimData();

	if (IsMovementOverrideSequence(animData->animGroupIDs[AnimData::kSequence_Weapon]))
		return false;

	return true;
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
	const NiVector3 &moveVector,
	bool inAir,
	float baseSpeed,
	float deltaTime)
{
	const auto speed = ((NiVector3&)*velocity).DotProduct(moveVector);
	const auto maxSpeed = inAir ? baseSpeed * ini::fAirSpeed : baseSpeed;
	const auto speedCap = std::max(baseSpeed, ((NiVector3&)*velocity).Length());

	if (speed >= maxSpeed)
		return;

	const auto accelMultiplier = inAir ? ini::fAirAcceleration : ini::fAcceleration;
	const auto scaleSpeed = std::max(baseSpeed, ini::fMinAccelScaleSpeed);
	const auto accel = accelMultiplier * scaleSpeed * move.groundNormal.z * deltaTime;
	*velocity += moveVector * std::min(accel, maxSpeed - speed);

	if (const auto newLength = ((NiVector3&)*velocity).Length(); newLength > speedCap)
		*velocity *= speedCap / newLength;
}

static AlignedVector4 GetInputVector(UInt32 moveFlags)
{
	auto result = AlignedVector4(0, 0, 0, 0);

	if (moveFlags & kMoveFlag_Forward)
		result.x = 1.f;
	else if (moveFlags & kMoveFlag_Backward)
		result.x = -1.f;

	if (moveFlags & kMoveFlag_Left)
		result.y = -1.f;
	else if (moveFlags & kMoveFlag_Right)
		result.y = 1.f;

	return result;
}

static AlignedVector4 GetMoveVector(const CharacterMoveParams &move, const AlignedVector4 &input)
{
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
	const auto inAir = state == kState_InAir || g_player.justLanded;

	if (!inAir)
		ApplyFriction(move, velocity, deltaTime);

	constexpr auto kMoveMask =
		kMoveFlag_Forward | kMoveFlag_Backward |
		kMoveFlag_Left    | kMoveFlag_Right;

	const auto *mover = (PlayerMover*)GetPlayer()->actorMover;

	if ((mover->pcMovementFlags & kMoveMask) != 0) {
		const auto inputVector = GetInputVector(mover->pcMovementFlags);
		const auto moveVector = GetMoveVector(move, inputVector);
		const auto moveSpeed = mover->moveSpeed * kHavokUnitScale;
		ApplyAcceleration(move, velocity, moveVector, inAir, moveSpeed, deltaTime);
	}
}

static void ApplyThrowback(bhkCharacterController *charCtrl)
{
	if (charCtrl->throwbackTimer <= 0.f || !charCtrl->chrListener.ReceivesThrowback())
		return;

	// Scale based on total distance moved in vanilla
	const auto scale = charCtrl->throwbackTimer * charCtrl->throwbackTimer * .5f;
	charCtrl->velocity += charCtrl->throwbackVelocity * (scale * ini::fKnockbackScale);
	charCtrl->throwbackTimer = 0.f;
	charCtrl->throwbackVelocity = AlignedVector4(0, 0, 0, 0);
}

static void hook_MoveCharacter(
	bhkCharacterController *charCtrl,
	CharacterMoveParams *move,
	AlignedVector4 *velocity)
{
	if (!ShouldUsePhysics(charCtrl)) {
		// call original
		CdeclCall(HookGetOriginal(), move, velocity);
		return;
	}

	const auto state = charCtrl->chrContext.hkState;
	const auto deltaTime = charCtrl->stepInfo.deltaTime;

	*velocity -= move->surfaceVelocity.PS();
	UpdateVelocity(*move, velocity, state, deltaTime);
	ApplyThrowback(charCtrl);
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

static int __fastcall hook_CheckJumpButton(
	OSInputGlobals *input, int, int key, ControlState state)
{
	if (ThisCall<int>(HookGetOriginal(), input, key, kControlState_Pressed)) {
		// Fresh input
		g_player.usedJumpInput = false;
		return true;
	} else if (g_player.usedJumpInput) {
		// Already used this input to jump
		return false;
	}
	return ThisCall<int>(HookGetOriginal(), input, key, kControlState_Held);
}

static bool WillJump(bhkCharacterController *charCtrl)
{
	// Check that we won't exit jump state early without setting velocity
	switch (charCtrl->wantState) {
	case kState_OnGround:
	case kState_Climbing:
		return false;
	default:
		return true;
	}
}

static void __fastcall hook_bhkCharacterStateJumping_UpdateVelocity(
	bhkCharacterStateJumping *state, int, bhkCharacterController *charCtrl)
{
	if (!ShouldUsePhysics(charCtrl) || !WillJump(charCtrl)) {
		ThisCall(HookGetOriginal(), state, charCtrl);
		return;
	}
	// Must repress jump input
	g_player.usedJumpInput = true;
	// Additive jumps
	const auto startZ = charCtrl->velocity.z;
	ThisCall(HookGetOriginal(), state, charCtrl);
	if (startZ > 0.f)
		charCtrl->velocity.z += startZ;
}

static bool WillFall(bhkCharacterController *charCtrl)
{
	return !(charCtrl->chrListener.flags & bhkCharacterListener::kHasSupport)
	    && !charCtrl->bFakeSupport;
}

static void __fastcall hook_bhkCharacterStateOnGround_UpdateVelocity(
	bhkCharacterStateOnGround *state, int, bhkCharacterController *charCtrl)
{
	// Preserve downward velocity when walking off things
	if (WillFall(charCtrl) && (!ShouldUsePhysics(charCtrl) || charCtrl->velocity.z > 0.f))
		charCtrl->velocity.z = 0.f;

	ThisCall(HookGetOriginal(), state, charCtrl);

	if (IsPlayerController(charCtrl))
		g_player.justLanded = false;
}

static void __fastcall hook_bhkCharacterStateInAir_UpdateVelocity(
	bhkCharacterStateInAir *state, int, bhkCharacterController *charCtrl)
{
	ThisCall(HookGetOriginal(), state, charCtrl);

	if (IsPlayerController(charCtrl) && charCtrl->chrContext.hkState == kState_OnGround)
		g_player.justLanded = true;
}

static void __fastcall hook_bhkCharacterController_UpdateCharacterState(
	bhkCharacterController *charCtrl, int, const void *params)
{
	if (IsPlayerController(charCtrl)) {
		charCtrl->gravityMult = ini::fGravityMult;
		charCtrl->chrListener.collisionTolerance = 0.f;
	}

	ThisCall(HookGetOriginal(), charCtrl, params);
}

static float __fastcall hook_bhkCharacterController_GetFallDistance(
	bhkCharacterController *charCtrl)
{
	// Prevent fake midair landing
	if (ShouldUsePhysics(charCtrl))
		return 1.f;

	return ThisCall<float>(HookGetOriginal(), charCtrl);
}

static void __fastcall hook_bhkCharacterController_UpdateThrowback(
	bhkCharacterController *charCtrl)
{
	// Handle throwback ourselves
	if (!ShouldUsePhysics(charCtrl))
		ThisCall(HookGetOriginal(), charCtrl);
}

static __declspec(naked) void hook_CheckToRootCharacter()
{
	// Don't root the player in place (when not driven by animation)
	__asm {
		push ebx
		call ShouldUsePhysics
		add esp, 4
		test al, al
		jne skip
		// Overwritten instruction
		cmp byte ptr [esp+0x1B], 0
		mov eax, 0xC73ACE
		jmp eax
	skip:
		mov eax, 0xC73C0D
		jmp eax
	}
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
	patch_call_rel32(0x94215F, hook_CheckJumpButton);
	patch_vtable(kVtbl_bhkCharacterStateJumping, 8, hook_bhkCharacterStateJumping_UpdateVelocity);
	patch_vtable(kVtbl_bhkCharacterStateOnGround, 8, hook_bhkCharacterStateOnGround_UpdateVelocity);
	patch_vtable(kVtbl_bhkCharacterStateInAir, 8, hook_bhkCharacterStateInAir_UpdateVelocity);
	patch_vtable(kVtbl_bhkCharacterController, 50, hook_bhkCharacterController_UpdateCharacterState);
	patch_call_rel32(0xCD400B, hook_bhkCharacterController_GetFallDistance);
	patch_call_rel32(0xCD47AB, hook_bhkCharacterController_UpdateThrowback);
	patch_call_rel32(0xCD4AA2, hook_bhkCharacterController_UpdateThrowback);
	patch_jmp_rel32(0xC73AC9, hook_CheckToRootCharacter);
	// Zero out bhkCharacterStateOnGround::clearZVelocityOnFall
	patch_code(0xCD47F1, "\xC6\x40\x08\x00\xC3");
	// Don't zero Z velocity with no input on ground
	patch_code(0xC7386E, "\x90\x90\x90\x90\x90\x90");
	// Allow jumping while aiming
	patch_code(0x9422AA, "\xEB");
	// Use standard ground collision when not giving input
	patch_code(0xC72025, "\xEB");
	// Don't factor speedPct into ground collisions
	patch_code(0xC7203A, "\xEB");
	return true;
}