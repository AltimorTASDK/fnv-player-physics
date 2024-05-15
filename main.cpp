#include <cstddef>
#include <Windows.h>

struct IniPrefSetting {
	void **vtable;
	float value;
	const char *name;

	IniPrefSetting(const char *name, float value)
	{
		ThisCall(0x4DE370, this, name, value);
	}
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
auto fAcceleration = IniPrefSetting("fAcceleration:Movement", 5.f);
auto fFriction = IniPrefSetting("fAcceleration:Friction", 5.f);
auto fStopSpeed = IniPrefSetting("fAcceleration:StopSpeed", .3f);
}

static void ApplyFriction(
	bhkCharacterController *charCtrl,
	const CharacterMoveParams &move,
	AlignedVector4 *velocity,
	float deltaTime)
{
	const auto speed = NiVector3(*velocity).Length();
	const auto stopSpeed = move.maxSpeed * ini::fStopSpeed.value;
	const auto friction = ini::fFriction.value * std::max(speed, stopSpeed) * dt;

	if (friction >= speed)
		*velocity = AlignedVector4(0, 0, 0, 0);
	else
		*velocity *= 1.f - friction / speed;
}

static void hook_MoveCharacter(
	bhkCharacterController *charCtrl,
	const CharacterMoveParams &move,
	AlignedVector4 *velocity)
{
	if (charCtrl != PlayerCharacter::GetSingleton()->GetCharacterController()) {
		// call original
		CdeclCall(0xD6AEF0, &move, velocity);
		return;
	}

	*velocity -= move.surfaceVelocity.PS();

	const auto deltaTime = charCtrl->stepInfo.deltaTime;

	ApplyFriction(charCtrl, move, velocity, deltaTime);

	const auto &forward = move.forward;
	const auto &up = move.up;
	const auto right = NiVector3(NiVector3(forward).CrossProduct(up));
	const auto input = forward * -move.input.x + right * move.input.y + up * move.input.z;
	*velocity += AlignedVector4(input) * .1f;

	*velocity += move.surfaceVelocity.PS();
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
	*(std::byte**)(addr + 1) = (std::byte*)dest - addr - 5;
	VirtualProtect((void*)addr, 5, old_protect, &old_protect);
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "Viewmodel Adjustment";
	info->version = 2;
	return true;
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Load(NVSEInterface *nvse)
{
	patch_call_rel32(0xCD414D, hook_MoveCharacter_wrapper);
	patch_call_rel32(0xCD45D0, hook_MoveCharacter_wrapper);
	patch_call_rel32(0xCD4A2A, hook_MoveCharacter_wrapper);
	return true;
}