#include "util/memory.h"
#include <Windows.h>

extern "C" extern int _tls_index;

void *rwx_byte::operator new[](size_t size)
{
	return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void rwx_byte::operator delete[](void *ptr)
{
	if (ptr != nullptr)
		VirtualFree(ptr, 0, MEM_RELEASE);
}

void patch_code(void *target, const void *patch, size_t size)
{
	DWORD old_protect;
	VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protect);
	memcpy(target, patch, size);
	VirtualProtect(target, size, old_protect, &old_protect);
}

namespace detail::hook {
static __declspec(naked) void __stdcall set_original(void *value)
{
	__asm {
		push eax
		push [esp+4+4]
		mov eax, _tls_index
		lea eax, [eax*4]
		add eax, fs:0x2C
		mov eax, [eax]
		pop [eax+original]
		pop eax
		ret 4
	}
}
}

void patch_call_rel32(const uintptr_t address, const void *hook)
{
	auto *trampoline = new rwx_byte[15];
	write_push(trampoline +  0, read_rel32(address));
	write_call(trampoline +  5, detail::hook::set_original);
	write_jmp (trampoline + 10, hook);

	DWORD old_protect;
	VirtualProtect((void*)address, 5, PAGE_EXECUTE_READWRITE, &old_protect);
	write_call((void*)address, trampoline);
	VirtualProtect((void*)address, 5, old_protect, &old_protect);
}
