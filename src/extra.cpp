#include <internal/jip_core.h>
#include <internal/hooks.h>
#include <internal/patches_cmd.h>
#include <internal/patches_game.h>

__declspec(naked) void __fastcall RefreshRecipeMenu(RecipeMenu *menu)
{
	_asm
	{
		push	esi
		push	edi
		mov		esi, ecx
		add		ecx, 0x6C
		mov		eax, [ecx]
		call	dword ptr [eax+0x1C]
		mov		ecx, esi
		push	dword ptr [ecx+0x64]
		push	0
		CALL_EAX(0x727680)
		add		esi, 0x6C
		mov		ecx, esi
		mov		edi, [ecx]
		push	0
		push	0
		call	dword ptr [edi+0x14]
		push	eax
		mov		ecx, esi
		call	dword ptr [edi]
		mov		ecx, esi
		call	dword ptr [edi+0x10]
		mov		ecx, esi
		CALL_EAX(0x7312E0)
		mov		eax, 0x727637
		push	dword ptr [eax]
		mov		ecx, esi
		CALL_EAX(0x729FE0)
		push	1
		mov		ecx, esi
		CALL_EAX(0x72A660)
		pop		edi
		pop		esi
		retn
	}
}