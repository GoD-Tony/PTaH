/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod P Tools and Hooks Extension
 * Copyright (C) 2004-2016 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"
#include "forwards.h"
#include "classes.h"

CForwardManager g_pPTaHForwards;

//GiveNamedItem
SH_DECL_MANUALHOOK4(GiveNamedItemHook, 0, 0, 0, CBaseEntity *, const char *, int, CEconItemView *, bool);
//GiveNamedItemPre
SH_DECL_MANUALHOOK4(GiveNamedItemPreHook, 0, 0, 0, CBaseEntity *, const char *, int, CEconItemView *, bool);
//WeaponCanUse
SH_DECL_MANUALHOOK1(WeaponCanUseHook, 0, 0, 0, bool, CBaseCombatWeapon *);
//SetModel
SH_DECL_MANUALHOOK1(SetModelHook, 0, 0, 0, CBaseEntity *, const char *);
//SetModelPre
SH_DECL_MANUALHOOK1(SetModelPreHook, 0, 0, 0, CBaseEntity *, const char *);
//ClientPrint
SH_DECL_HOOK2_void(IVEngineServer, ClientPrintf, SH_NOATTRIB, 0, edict_t *, const char *);
//ConnectClient
SH_DECL_MANUALHOOK13(ConnectClient, 0, 0, 0, IClient *, const netadr_t &, int, int, int, const char *, const char *, const char *, int, CUtlVector<NetMsg_SplitPlayerConnect *> &, bool, CrossPlayPlatform_t, const unsigned char *, int);
//RejectConnection
SH_DECL_MANUALHOOK1_void_vafmt(RejectConnection, 0, 0, 0, const netadr_t &);

DETOUR_DECL_MEMBER1(CDownloadListGenerator, void, const char *, file_name)
{
    if (file_name != NULL && g_pPTaHForwards.m_pMapContentList->GetFunctionCount() > 0)
    {
        char sFileByf[256];
        cell_t res = PLUGIN_CONTINUE;
        strncpy(sFileByf, file_name, 256);
        g_pPTaHForwards.m_pMapContentList->PushStringEx(sFileByf, 256, SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
        g_pPTaHForwards.m_pMapContentList->Execute(&res);
		if(res != Pl_Continue)
		{
			if(res == Pl_Changed) DETOUR_MEMBER_CALL(CDownloadListGenerator)(sFileByf);
			return;
		}
    }
    DETOUR_MEMBER_CALL(CDownloadListGenerator)(file_name);
    return;
}

DETOUR_DECL_MEMBER1(ExecuteStringCommand, bool, const char *, szMsg)
{
	if(szMsg != NULL && g_pPTaHForwards.m_pExecuteStringCommand->GetFunctionCount() > 0)
	{
		char szMsg_buf[512];
		strcpy(szMsg_buf, szMsg);
		cell_t res = PLUGIN_CONTINUE;
		
		#ifdef WIN32
		int client = reinterpret_cast<IClient *>(this)->GetPlayerSlot()+1;
		#else
		int client = reinterpret_cast<IClient *>(this+4)->GetPlayerSlot()+1;
		#endif
		
		g_pPTaHForwards.m_pExecuteStringCommand->PushCell(client);
		g_pPTaHForwards.m_pExecuteStringCommand->PushStringEx(szMsg_buf, sizeof(szMsg_buf), SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		g_pPTaHForwards.m_pExecuteStringCommand->Execute(&res);
		
		if(res != Pl_Continue)
		{
			if(res == Pl_Changed) return DETOUR_MEMBER_CALL(ExecuteStringCommand)(szMsg_buf);
			else return false;
		}
	}
	return DETOUR_MEMBER_CALL(ExecuteStringCommand)(szMsg);
}

bool CForwardManager::Init()
{
	int offset = -1;
	
	if(!g_pGameConf[GameConf_SDKT]->GetOffset("GiveNamedItem", &offset))
	{
		smutils->LogError(myself, "Failed to get GiveNamedItem offset.");
		return false;
	}
	SH_MANUALHOOK_RECONFIGURE(GiveNamedItemHook, offset, 0, 0);
	SH_MANUALHOOK_RECONFIGURE(GiveNamedItemPreHook, offset, 0, 0);
	
	if(!g_pGameConf[GameConf_SDKT]->GetOffset("SetEntityModel", &offset))
	{
		smutils->LogError(myself, "Failed to get SetEntityModel offset.");
		return false;
	}
	
	SH_MANUALHOOK_RECONFIGURE(SetModelHook, offset, 0, 0);
	SH_MANUALHOOK_RECONFIGURE(SetModelPreHook, offset, 0, 0);
	
	if(!g_pGameConf[GameConf_SDKH]->GetOffset("Weapon_CanUse", &offset))
	{
		smutils->LogError(myself, "Failed to get Weapon_CanUse offset.");
		return false;
	}	
	SH_MANUALHOOK_RECONFIGURE(WeaponCanUseHook, offset, 0, 0);
	
	m_pCDownloadListGenerator = DETOUR_CREATE_MEMBER(CDownloadListGenerator, "CDownloadListGenerator");
	if (!m_pCDownloadListGenerator)
	{
		smutils->LogError(myself, "Detour failed CDownloadListGenerator.");
		return false;
	}
	else m_pCDownloadListGenerator->EnableDetour();
	
	if (!g_pGameConf[GameConf_PTaH]->GetOffset("ConnectClient", &offset) || offset == -1)
	{
		smutils->LogError(myself, "Failed to get ConnectClient offset.");
		return false;
	}
	SH_MANUALHOOK_RECONFIGURE(ConnectClient, offset, 0, 0);
	
	if (!g_pGameConf[GameConf_PTaH]->GetOffset("RejectConnection", &offset) || offset == -1)
	{
		smutils->LogError(myself, "Failed to get RejectConnection offset.");
		return false;
	}
	SH_MANUALHOOK_RECONFIGURE(RejectConnection, offset, 0, 0);
	
	m_pDExecuteStringCommand = DETOUR_CREATE_MEMBER(ExecuteStringCommand, "ExecuteStringCommand");
	if (!m_pDExecuteStringCommand)
	{
		smutils->LogError(myself, "Detour failed ExecuteStringCommand.");
		return false;
	}
	else m_pDExecuteStringCommand->EnableDetour();
	
	SH_ADD_HOOK(IVEngineServer, ClientPrintf, engine, SH_MEMBER(this, &CForwardManager::ClientPrint), false);
	SH_ADD_MANUALHOOK(ConnectClient, iserver, SH_MEMBER(this, &CForwardManager::OnClientConnect), false);

	
	m_pGiveNamedItem = forwards->CreateForwardEx(NULL, ET_Ignore, 4, NULL, Param_Cell, Param_String, Param_Cell, Param_Cell);
	m_pGiveNamedItemPre = forwards->CreateForwardEx(NULL, ET_Hook, 3, NULL, Param_Cell, Param_String, Param_CellByRef);
	m_pWeaponCanUse = forwards->CreateForwardEx(NULL, ET_Hook, 3, NULL, Param_Cell, Param_Cell, Param_Cell);
	m_pSetModel = forwards->CreateForwardEx(NULL, ET_Ignore, 2, NULL, Param_Cell, Param_String);
	m_pSetModelPre = forwards->CreateForwardEx(NULL, ET_Hook, 3, NULL, Param_Cell, Param_String, Param_String);
	m_pClientPrintf = forwards->CreateForwardEx(NULL, ET_Hook, 2, NULL, Param_Cell, Param_String);
	m_pMapContentList = forwards->CreateForwardEx(NULL, ET_Hook, 1, NULL, Param_String);
	m_pOnClientConnect = forwards->CreateForwardEx(NULL, ET_Hook, 5, NULL, Param_String, Param_String, Param_String, Param_String, Param_String);
	m_pExecuteStringCommand = forwards->CreateForwardEx(NULL, ET_Hook, 2, NULL, Param_Cell, Param_String);
	
	return true;
}

void CForwardManager::Shutdown()
{
	forwards->ReleaseForward(m_pGiveNamedItem);
	forwards->ReleaseForward(m_pGiveNamedItemPre);
	forwards->ReleaseForward(m_pWeaponCanUse);
	forwards->ReleaseForward(m_pSetModel);
	forwards->ReleaseForward(m_pSetModelPre);
	forwards->ReleaseForward(m_pClientPrintf);
	forwards->ReleaseForward(m_pMapContentList);
	forwards->ReleaseForward(m_pOnClientConnect);
	forwards->ReleaseForward(m_pExecuteStringCommand);
}

void CForwardManager::HookClient(int client)
{
	CBaseEntity *pEnt = gamehelpers->ReferenceToEntity(client);
	if(pEnt)
	{
		m_iHookId[PTaH_GiveNamedItem][client] = SH_ADD_MANUALHOOK(GiveNamedItemHook, pEnt, SH_MEMBER(this, &CForwardManager::GiveNamedItem), true);
		m_iHookId[PTaH_GiveNamedItemPre][client] = SH_ADD_MANUALHOOK(GiveNamedItemPreHook, pEnt, SH_MEMBER(this, &CForwardManager::GiveNamedItemPre), false);
		m_iHookId[PTaH_WeaponCanUse][client] = SH_ADD_MANUALHOOK(WeaponCanUseHook, pEnt, SH_MEMBER(this, &CForwardManager::WeaponCanUse), true);
		m_iHookId[PTaH_SetPlayerModel][client] = SH_ADD_MANUALHOOK(SetModelHook, pEnt, SH_MEMBER(this, &CForwardManager::SetModel), true);
		m_iHookId[PTaH_SetPlayerModelPre][client] = SH_ADD_MANUALHOOK(SetModelPreHook, pEnt, SH_MEMBER(this, &CForwardManager::SetModelPre), false);
	}
}

void CForwardManager::UnhookClient(int client)
{
	if(m_iHookId[PTaH_GiveNamedItem][client] != 0)
	{
		SH_REMOVE_HOOK_ID(m_iHookId[PTaH_GiveNamedItem][client]);
		m_iHookId[PTaH_GiveNamedItem][client] = 0;
	}
	if(m_iHookId[PTaH_GiveNamedItemPre][client] != 0)
	{
		SH_REMOVE_HOOK_ID(m_iHookId[PTaH_GiveNamedItemPre][client]);
		m_iHookId[PTaH_GiveNamedItemPre][client] = 0;
	}
	if(m_iHookId[PTaH_WeaponCanUse][client] != 0)
	{
		SH_REMOVE_HOOK_ID(m_iHookId[PTaH_WeaponCanUse][client]);
		m_iHookId[PTaH_WeaponCanUse][client] = 0;
	}
	if(m_iHookId[PTaH_SetPlayerModel][client] != 0)
	{
		SH_REMOVE_HOOK_ID(m_iHookId[PTaH_SetPlayerModel][client]);
		m_iHookId[PTaH_SetPlayerModel][client] = 0;
	}
	if(m_iHookId[PTaH_SetPlayerModelPre][client] != 0)
	{
		SH_REMOVE_HOOK_ID(m_iHookId[PTaH_SetPlayerModelPre][client]);
		m_iHookId[PTaH_SetPlayerModelPre][client] = 0;
	}
}


CBaseEntity *CForwardManager::GiveNamedItem(const char *szItem, int iSubType, CEconItemView *pView, bool removeIfNotCarried)
{
	if(m_pGiveNamedItem->GetFunctionCount() > 0)
	{
		CBaseEntity *pEnt = META_IFACEPTR(CBaseEntity);
		
		m_pGiveNamedItem->PushCell(gamehelpers->EntityToBCompatRef(pEnt));
		m_pGiveNamedItem->PushString(szItem);
		m_pGiveNamedItem->PushCell((cell_t)pView);
		m_pGiveNamedItem->PushCell(gamehelpers->EntityToBCompatRef(META_RESULT_ORIG_RET(CBaseEntity *)));
		m_pGiveNamedItem->Execute(nullptr);
	}
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

CBaseEntity *CForwardManager::GiveNamedItemPre(const char *szItem, int iSubType, CEconItemView *pView, bool removeIfNotCarried)
{
	if(m_pGiveNamedItemPre->GetFunctionCount() > 0)
	{
		char szItemByf[64];
		strcpy(szItemByf, szItem);
		cell_t pViewNew = ((cell_t)pView);
		cell_t res = PLUGIN_CONTINUE;
		m_pGiveNamedItemPre->PushCell(gamehelpers->EntityToBCompatRef(META_IFACEPTR(CBaseEntity)));
		m_pGiveNamedItemPre->PushStringEx(szItemByf, sizeof(szItemByf), SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		m_pGiveNamedItemPre->PushCellByRef(&pViewNew);
		m_pGiveNamedItemPre->Execute(&res);

		if(res != Pl_Continue)
		{
			if(res == Pl_Changed) RETURN_META_VALUE_MNEWPARAMS(MRES_HANDLED, NULL, GiveNamedItemPreHook, (((const char *)szItemByf), iSubType, ((CEconItemView *)pViewNew), removeIfNotCarried));
			else RETURN_META_VALUE(MRES_SUPERCEDE, nullptr);
		}
	}
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

bool CForwardManager::WeaponCanUse(CBaseCombatWeapon *pWeapon)
{
	if(m_pWeaponCanUse->GetFunctionCount() > 0)
	{
		cell_t res = META_RESULT_ORIG_RET(bool);
		cell_t ret = res;
		m_pWeaponCanUse->PushCell(gamehelpers->EntityToBCompatRef(META_IFACEPTR(CBaseEntity)));
		m_pWeaponCanUse->PushCell(gamehelpers->EntityToBCompatRef(pWeapon));
		m_pWeaponCanUse->PushCell(ret);
		m_pWeaponCanUse->Execute(&res);
		if(ret != res)
		{
			RETURN_META_VALUE(MRES_SUPERCEDE, !!res);
		}
	}
	RETURN_META_VALUE(MRES_IGNORED, true);
}

CBaseEntity *CForwardManager::SetModel(const char *sModel)
{
	if(m_pSetModel->GetFunctionCount() > 0)
	{
		m_pSetModel->PushCell(gamehelpers->EntityToBCompatRef(META_IFACEPTR(CBaseEntity)));
		m_pSetModel->PushString(sModel);
		m_pSetModel->Execute(nullptr);
	}
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

CBaseEntity *CForwardManager::SetModelPre(const char *sModel)
{
	if(m_pSetModelPre->GetFunctionCount() > 0)
	{
		CBaseEntity *pEnt = META_IFACEPTR(CBaseEntity);
		char sModelNew[128];
		strcpy(sModelNew, sModel);
		cell_t res = PLUGIN_CONTINUE;
		m_pSetModelPre->PushCell(gamehelpers->EntityToBCompatRef(pEnt));
		m_pSetModelPre->PushString(playerhelpers->GetGamePlayer(gamehelpers->EntityToBCompatRef(pEnt))->GetPlayerInfo()->GetModelName());
		m_pSetModelPre->PushStringEx(sModelNew, sizeof(sModelNew), SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		m_pSetModelPre->Execute(&res);
		
		if(res != Pl_Continue)
		{
			if(res == Pl_Changed) RETURN_META_VALUE_MNEWPARAMS(MRES_HANDLED, NULL, SetModelPreHook, (((const char *)sModelNew)));
			else RETURN_META_VALUE(MRES_SUPERCEDE, nullptr);
		}
	}
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

void CForwardManager::ClientPrint(edict_t *pEdict, const char *szMsg)
{
	if(m_pClientPrintf->GetFunctionCount() > 0)
	{
		char cMsg[512];
		strcpy(cMsg, szMsg);
		cell_t res = PLUGIN_CONTINUE;
		m_pClientPrintf->PushCell(gamehelpers->IndexOfEdict(pEdict));
		m_pClientPrintf->PushStringEx(cMsg, sizeof(cMsg), SM_PARAM_STRING_UTF8|SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		m_pClientPrintf->Execute(&res);
		
		if(res != Pl_Continue)
		{
			if(res == Pl_Changed) RETURN_META_NEWPARAMS(MRES_IGNORED, &IVEngineServer::ClientPrintf, (pEdict, ((const char *)cMsg)));
			else RETURN_META(MRES_SUPERCEDE);
		}
	}
	RETURN_META(MRES_IGNORED);
}

// https://github.com/peace-maker/sourcetvmanager/blob/master/forwards.cpp
// Thanks Peace-Maker!
static const char *ExtractPlayerName(CUtlVector<NetMsg_SplitPlayerConnect *> &pSplitPlayerConnectVector)
{
	for (int i = 0; i < pSplitPlayerConnectVector.Count(); i++)
	{
		NetMsg_SplitPlayerConnect *split = pSplitPlayerConnectVector[i];
		if (!split->has_convars())
			continue;

		const CMsg_CVars cvars = split->convars();
		for (int c = 0; c < cvars.cvars_size(); c++)
		{
			const CMsg_CVars_CVar cvar = cvars.cvars(c);
			if (!cvar.has_name() || !cvar.has_value())
				continue;

			if (!strcmp(cvar.name().c_str(), "name"))
			{
				return cvar.value().c_str();
			}
		}
	}
	return "";
}

IClient *CForwardManager::OnClientConnect(const netadr_t & address, int nProtocol, int iChallenge, int nAuthProtocol, const char *pchName, const char *pchPassword, const char *pCookie, int cbCookie, CUtlVector<NetMsg_SplitPlayerConnect *> &pSplitPlayerConnectVector, bool bUnknown, CrossPlayPlatform_t platform, const unsigned char *pUnknown, int iUnknown)
{
	if(nAuthProtocol == 3 && m_pOnClientConnect->GetFunctionCount() > 0)
	{
		cell_t res = PLUGIN_CONTINUE;
		
		char rejectReason[512];
		char szSteamID[32];
		char passwordBuffer[128];
		char ipString[16];
		
		V_strncpy(passwordBuffer, pchPassword, sizeof(passwordBuffer));
		V_snprintf(ipString, sizeof(ipString), "%u.%u.%u.%u", address.ip[0], address.ip[1], address.ip[2], address.ip[3]);
		uint64 ullSteamID = *(uint64 *)pCookie;
		CSteamID SteamID = CSteamID(ullSteamID);
		V_snprintf(szSteamID, sizeof(szSteamID), "STEAM_1:%u:%u", (SteamID.GetAccountID() % 2) ? 1 : 0, (int32)(SteamID.GetAccountID()/2));
		
		m_pOnClientConnect->PushString(ExtractPlayerName(pSplitPlayerConnectVector));
		m_pOnClientConnect->PushStringEx(passwordBuffer, sizeof(passwordBuffer), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		m_pOnClientConnect->PushString(ipString);
		m_pOnClientConnect->PushString(szSteamID);
		m_pOnClientConnect->PushStringEx(rejectReason, sizeof(rejectReason), SM_PARAM_STRING_UTF8 | SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
		m_pOnClientConnect->Execute(&res);
		
		if(res != Pl_Continue)
		{
			if(res == Pl_Changed)
			{
				//Not work:( RETURN_META_VALUE_MNEWPARAMS(MRES_HANDLED, nullptr, OnClientConnect, (address, nProtocol, iChallenge, nAuthProtocol, pchName, ((const char *)passwordBuffer), pCookie, cbCookie, pSplitPlayerConnectVector, bUnknown, platform, pUnknown, iUnknown));
				RETURN_META_VALUE(MRES_SUPERCEDE, SH_MCALL(iserver, ConnectClient)(address, nProtocol, iChallenge, nAuthProtocol, pchName, passwordBuffer, pCookie, cbCookie, pSplitPlayerConnectVector, bUnknown, platform, pUnknown, iUnknown));
			}
			else
			{
				SH_MCALL(iserver, RejectConnection)(address, rejectReason);
				RETURN_META_VALUE(MRES_SUPERCEDE, nullptr);
			}
		}
	}
	RETURN_META_VALUE(MRES_IGNORED, nullptr);
}