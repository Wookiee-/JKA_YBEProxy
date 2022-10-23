#include "Proxy_Header.hpp"

typedef struct cvarTable_s {
	vmCvar_t*	vmCvar;
	char*		cvarName;
	char*		defaultString;
	int			cvarFlags;
	int			modificationCount;  // for tracking changes
	qboolean	trackChange;	    // track this variable, and announce if changed
} cvarTable_t;

static cvarTable_t proxyOriginalEngineCVarTable[] =
{
	{ &proxy.originalEngineCvars.proxy_sv_pingFix, "proxy_sv_pingFix", "1", CVAR_ARCHIVE, 0, qfalse },
};
static const constexpr size_t proxyOriginalEngineCVarTableSize = ARRAY_LEN(proxyOriginalEngineCVarTable);

static cvarTable_t proxyCVarTable[] =
{
	// New cvars
	{ &proxy.cvars.proxy_sv_maxCallVoteMapRestartValue, "proxy_sv_maxCallVoteMapRestartValue", "60", CVAR_ARCHIVE, 0, qfalse },

	// get cvars
	{ &proxy.cvars.sv_fps, "sv_fps", "20", CVAR_SERVERINFO, 0, qfalse }
};
static const constexpr size_t proxyCVarTableSize = ARRAY_LEN(proxyCVarTable);

// ==================================================
// Proxy_OriginalEngine_CVars_Registration
// --------------------------------------------------
// Those are the variables that will affect engine modifications.
// ==================================================

void Proxy_OriginalEngine_CVars_Registration(void)
{
	unsigned int i = 0;
	cvarTable_t* currentCVarTableItem = nullptr;

	for (i = 0, currentCVarTableItem = proxyOriginalEngineCVarTable; i < proxyOriginalEngineCVarTableSize; ++i, ++currentCVarTableItem)
	{
		proxy.trap->Cvar_Register(currentCVarTableItem->vmCvar, currentCVarTableItem->cvarName, currentCVarTableItem->defaultString, currentCVarTableItem->cvarFlags);

		if (currentCVarTableItem->vmCvar)
		{
			currentCVarTableItem->modificationCount = currentCVarTableItem->vmCvar->modificationCount;
		}
	}
}

// ==================================================
// Proxy_CVars_Registration
// --------------------------------------------------
// Those are the variables that won't affect engine modifications.
// ==================================================

void Proxy_CVars_Registration(void)
{
	unsigned int i = 0;
	cvarTable_t* currentCVarTableItem = nullptr;

	for (i = 0, currentCVarTableItem = proxyCVarTable; i < proxyCVarTableSize; ++i, ++currentCVarTableItem)
	{
		proxy.trap->Cvar_Register(currentCVarTableItem->vmCvar, currentCVarTableItem->cvarName, currentCVarTableItem->defaultString, currentCVarTableItem->cvarFlags);

		if (currentCVarTableItem->vmCvar)
		{
			currentCVarTableItem->modificationCount = currentCVarTableItem->vmCvar->modificationCount;
		}
	}
}

// ==================================================
// Proxy_OriginalEngine_UpdateCvars
// --------------------------------------------------
// Updates all affecting engine variables.
// ==================================================

void Proxy_OriginalEngine_UpdateCvars(void)
{
	unsigned int i = 0;
	cvarTable_t* currentCVarTable = nullptr;

	for (i = 0, currentCVarTable = proxyOriginalEngineCVarTable; i < proxyOriginalEngineCVarTableSize; ++i, ++currentCVarTable)
	{
		if (currentCVarTable->vmCvar)
		{
			int vmCvarModificationCount = currentCVarTable->vmCvar->modificationCount;
			proxy.trap->Cvar_Update(currentCVarTable->vmCvar);

			if (currentCVarTable->modificationCount != vmCvarModificationCount)
			{
				currentCVarTable->modificationCount = vmCvarModificationCount;

				if (currentCVarTable->trackChange)
				{
					proxy.trap->SendServerCommand(-1, va("print \"Server: %s changed to %s\n\"",
						currentCVarTable->cvarName, currentCVarTable->vmCvar->string));
				}
			}
		}
	}
}

// ==================================================
// Proxy_UpdateCvars
// --------------------------------------------------
// Updates all proxy variables.
// ==================================================

void Proxy_UpdateCvars(void)
{
	unsigned int i = 0;
	cvarTable_t* currentCVarTable = nullptr;

	for (i = 0, currentCVarTable = proxyCVarTable; i < proxyCVarTableSize; ++i, ++currentCVarTable)
	{
		if (currentCVarTable->vmCvar)
		{
			int vmCvarModificationCount = currentCVarTable->vmCvar->modificationCount;
			proxy.trap->Cvar_Update(currentCVarTable->vmCvar);

			if (currentCVarTable->modificationCount != vmCvarModificationCount)
			{
				currentCVarTable->modificationCount = vmCvarModificationCount;

				if (currentCVarTable->trackChange)
				{
					proxy.trap->SendServerCommand(-1, va("print \"Server: %s changed to %s\n\"",
						currentCVarTable->cvarName, currentCVarTable->vmCvar->string));
				}
			}
		}
	}
}

// ==================================================
// Proxy_UpdateAllCvars
// --------------------------------------------------
// Updates all the variables once per second~.
// ==================================================

void Proxy_UpdateAllCvars(void)
{
	// TODO: won't be once per second on sv_fps change
	static unsigned int frame = 0;

	if (frame == proxy.cvars.sv_fps.integer)
	{
		Proxy_UpdateCvars();

		if (proxy.isOriginalEngine)
		{
			Proxy_OriginalEngine_UpdateCvars();
		}

		frame = 0;
	}

	frame++;
}
