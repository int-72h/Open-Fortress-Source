//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: Weapon Medkit.
//
//=============================================================================
#include "cbase.h"
#include "tf_weapon_medkit.h"
#include "tf_gamerules.h"

#ifdef GAME_DLL
#include "tf_gamestats.h"
#include "ilagcompensationmanager.h"
#endif

//=============================================================================
//
// Weapon Medkit tables.
//
IMPLEMENT_NETWORKCLASS_ALIASED(TFMedkit, DT_TFCWeaponMedkit)

BEGIN_NETWORK_TABLE(CTFMedkit, DT_TFCWeaponMedkit)
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA(CTFMedkit)
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS(tfc_weapon_medkit, CTFMedkit);
//PRECACHE_WEAPON_REGISTER( tfc_weapon_medkit );

//=============================================================================
//
// Weapon Medkit functions.
//

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTFMedkit::CTFMedkit()
{
}

//-----------------------------------------------------------------------------
// Purpose: Set stealth attack bool
//-----------------------------------------------------------------------------
void CTFMedkit::PrimaryAttack(void)
{
	CTFPlayer *pPlayer = ToTFPlayer(GetPlayerOwner());

	if (!CanAttack())
		return;

	// Set the weapon usage mode - primary, secondary.
	m_iWeaponMode = TF_WEAPON_PRIMARY_MODE;

#if !defined (CLIENT_DLL)
	// Move other players back to history positions based on local player's lag
	lagcompensation->StartLagCompensation(pPlayer, pPlayer->GetCurrentCommand());
#endif

	trace_t trace;
	if (DoSwingTrace(trace) == true)
	{
		// we will hit something with the attack
		if (trace.m_pEnt && (trace.m_pEnt->IsPlayer() || trace.m_pEnt->IsNPC())) // npcs too!
		{
			CBaseCombatCharacter *pTarget = trace.m_pEnt->MyCombatCharacterPointer();

			if (pTarget)
			{
				m_iWeaponMode = TF_WEAPON_SECONDARY_MODE;

				Swing(pPlayer);

				// store the victim to compare when we do the damage
				m_hVictim = trace.m_pEnt;
			}
		}
		else if (!trace.m_pEnt)
		{
			SwingMiss(pPlayer);
		}
	}

#if !defined (CLIENT_DLL)
	lagcompensation->FinishLagCompensation(pPlayer);
#endif

#if !defined( CLIENT_DLL ) 
	pPlayer->SpeakWeaponFire();
	CTF_GameStats.Event_PlayerFiredWeapon(pPlayer, IsCurrentAttackACritical());
#endif
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//-----------------------------------------------------------------------------
void CTFMedkit::Swing(CTFPlayer *pPlayer)
{
#ifdef GAME_DLL
	pPlayer->m_Shared.RemoveCond(TF_COND_SPAWNPROTECT);
#endif

	// Play the melee swing and miss (whoosh) always.
	pPlayer->DoAnimationEvent(PLAYERANIMEVENT_ATTACK_PRIMARY);

	Activity act = ACT_VM_SWINGHARD;
	SendWeaponAnim(act);

	if (GetTFWpnData().m_WeaponData[TF_WEAPON_PRIMARY_MODE].m_flBurstFireDelay == 0)
		// Set next attack times.
		m_flNextPrimaryAttack = gpGlobals->curtime + GetFireRate();

	SetWeaponIdleTime(m_flNextPrimaryAttack + m_pWeaponInfo->GetWeaponData(m_iWeaponMode).m_flTimeIdleEmpty);

	//WeaponSound( BURST );

	m_flSmackTime = gpGlobals->curtime + m_pWeaponInfo->GetWeaponData(m_iWeaponMode).m_flSmackDelay;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//-----------------------------------------------------------------------------
void CTFMedkit::SwingMiss(CTFPlayer *pPlayer)
{
#ifdef GAME_DLL
	pPlayer->m_Shared.RemoveCond(TF_COND_SPAWNPROTECT);
#endif

	// Play the melee swing and miss (whoosh) always.
	pPlayer->DoAnimationEvent(PLAYERANIMEVENT_ATTACK_PRIMARY);

	if (GetTFWpnData().m_WeaponData[TF_WEAPON_PRIMARY_MODE].m_flBurstFireDelay == 0)
		// Set next attack times.
		m_flNextPrimaryAttack = gpGlobals->curtime + GetFireRate();

	WeaponSound(MELEE_MISS);
}

//-----------------------------------------------------------------------------
// Purpose: Do backstab damage
//-----------------------------------------------------------------------------
float CTFMedkit::GetMeleeDamage(CBaseEntity *pTarget, int &iCustomDamage)
{
	CTFPlayer *pPlayer = ToTFPlayer(GetPlayerOwner());

#ifdef GAME_DLL
	CTFPlayer *pTFPlayer = ToTFPlayer(pTarget);
#endif 

	float flBaseDamage = (m_pWeaponInfo->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nDamage);

	if (pTarget->IsPlayer() || pTarget->IsNPC()) // damage npcs too
	{
		if (m_iWeaponMode == TF_WEAPON_SECONDARY_MODE && m_hVictim.Get() == pTarget)
		{
			if (GetEnemyTeam(pTarget) == pPlayer->GetTeamNumber())
			{
#ifdef GAME_DLL
				iCustomDamage = TF_DMG_CUSTOM_POISON;

				CTakeDamageInfo info;

				info.SetAttacker(GetOwnerEntity());		// the player who operated the thing that emitted nails
				info.SetInflictor(pPlayer);				// the weapon that emitted this projectile

				pTFPlayer->m_Shared.Poison(pPlayer, GetTFWpnData().m_flEffectDuration);
#endif
			}
			else
			{
#ifdef GAME_DLL
				int iHealthRestored = 0;
				int iHealthToAdd = flBaseDamage;

				iHealthToAdd = clamp(iHealthToAdd, 0, pTFPlayer->m_Shared.GetMaxBuffedHealth() - pTFPlayer->GetHealth());
				iHealthRestored = pTFPlayer->TakeHealth(iHealthToAdd, DMG_IGNORE_MAXHEALTH);

				flBaseDamage = 0;

				pPlayer->SpeakConceptIfAllowed(MP_CONCEPT_MEDIC_STOPPEDHEALING, pTFPlayer->IsAlive() ? "healtarget:alive" : "healtarget:dead");
				pTFPlayer->SpeakConceptIfAllowed(MP_CONCEPT_HEALTARGET_STOPPEDHEALING);

				//The TFC Medkit stops burning...
				if (pTFPlayer->m_Shared.InCond(TF_COND_BURNING))
					pTFPlayer->m_Shared.RemoveCond(TF_COND_BURNING);
				//...and Infection...
				if (pTFPlayer->m_Shared.InCond(TF_COND_POISON))
					pTFPlayer->m_Shared.RemoveCond(TF_COND_POISON);
				//...and Tranq...
				if (pTFPlayer->m_Shared.InCond(TF_COND_TRANQ))
					pTFPlayer->m_Shared.RemoveCond(TF_COND_TRANQ);
				//...and Prieced Legs...
				if (pTFPlayer->m_Shared.InCond(TF_COND_FUCKEDUP_LEGS))
					pTFPlayer->m_Shared.RemoveCond(TF_COND_FUCKEDUP_LEGS);
#endif
			}
		}
	}

	return flBaseDamage;
}
