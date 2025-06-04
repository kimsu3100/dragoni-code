#include "StdAfx.h"
#include "PgItemRarityUpgradeFormula.h"
#include "unit.h"

PgItemRarityUpgradeFormula::PgItemRarityUpgradeFormula()
{
}

double const PgItemRarityUpgradeFormula::GetRarityUpgradeCostRate(int iEquipPos)
{
	CONT_ITEM_RARITY_UPGRADE_COST_RATE const * pCont = NULL;
	g_kTblDataMgr.GetContDef(pCont);
	CONT_ITEM_RARITY_UPGRADE_COST_RATE::const_iterator const_iter = pCont->find(iEquipPos);
	if(const_iter == pCont->end())
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 1.0"));
		return 1.0;
	}
	return static_cast<double>((*const_iter).second.iCostRate)/100.0;
}

int	const PgItemRarityUpgradeFormula::GetItemRarityContorolType(EItemModifyParentEventType const kCause)
{
	switch(kCause)
	{
	case CIE_Make:
		return GIOT_MAKING;
	case CIE_Mission:
		return GIOT_MISSION;
	case CIE_Mission1:
		return GIOT_MISSION1;
	case CIE_Mission2:
		return GIOT_MISSION2;
	case CIE_Mission3:
		return GIOT_MISSION3;
	case CIE_Mission4:
		return GIOT_MISSION4;
	case CIE_Mission_GadaCoin:
		return GIOT_MISSION_GADACOIN;
	case CIE_Mission_Rank:
		return GIOT_MISSION_RANK;
	//case CIE_Reward:
	//	return GIOT_QUEST;
	case CIE_EnchantLvUp:
	case CIE_SoulCraft:
	case CIE_BasicOptionAmp:
		return GIOT_MAKING;
	case CIE_OpenChest:
	case CIE_OpenPack:
		return GIOT_CHEST;
	case CIE_OpenPack2:
	default:
		return GIOT_NONE;
	}
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return GIOT_NONE"));
	return GIOT_NONE;
}

bool const PgItemRarityUpgradeFormula::GetNeedCrystalInfo(PgBase_Item const & kItem, int const iElement, int & iCrystalNo, int & iCrystalCount, bool bIsSecond)
{
	CONT_FIVE_ELEMENT_INFO const * pContDefFiveElementInfo = NULL;
	g_kTblDataMgr.GetContDef(pContDefFiveElementInfo);
	
	CONT_FIVE_ELEMENT_INFO::const_iterator five_itor = pContDefFiveElementInfo->find(iElement);
	if(five_itor == pContDefFiveElementInfo->end())
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return false"));
		return false;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return false"));
		return false;
	}

	iCrystalNo = (*five_itor).second.iCrystalStoneNo;
	if(!bIsSecond)
	{
		iCrystalCount = static_cast<int>(pItemDef->GetAbil(AT_LEVELLIMIT) * 3 * GetRarityUpgradeCostRate(pItemDef->EquipPos()));
	}
	else
	{
		iCrystalCount = pItemDef->GetAbil(AT_LEVELLIMIT) * 6;
		iCrystalCount *= static_cast<int>(pow((kItem.EnchantInfo().AttrLv() + 1.0),1.2) * GetRarityUpgradeCostRate(pItemDef->EquipPos()));
	}

	return true;
}

E_ITEM_GRADE const GetNextGrade(E_ITEM_GRADE const eCurItemGrade)
{
	return __min(E_ITEM_GRADE(eCurItemGrade + 1),IG_EPIC);
}

int const PgItemRarityUpgradeFormula::GetExceptionRate(E_ITEM_GRADE const eItemGrade, PgBase_Item const & kItem)
{
	switch(eItemGrade)
	{
	case IG_NORMAL:
	case IG_RARE:
	case IG_UNIQUE:
	case IG_ARTIFACT:
	case IG_LEGEND:	
	case IG_EPIC:
		{	
			//return 40 * (100 - (int)kItem.EnchantInfo().Rarity()) / GetNextGrade(eItemGrade); // old
			return static_cast<int>((1/std::max(pow((eItemGrade+2),2.6),0.0))*10000.0f);
		}break;
	case IG_CURSE:		
		{
			return 8000;
		}break;
	case IG_SEAL:
		{
			return 10000;
		}break;
	}
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
	return 0;
}

int const PgItemRarityUpgradeFormula::GetNeedSoulCount(E_ITEM_GRADE const eItemGrade, PgBase_Item const & kItem, CUnit const * pkUnit, int const iAddDecSoulRate)
{
	if(NULL == pkUnit)
	{
		return 0;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
		return 0;
	}

	switch(eItemGrade)
	{
	case IG_NORMAL:
	case IG_RARE:
	case IG_UNIQUE:
	case IG_ARTIFACT:
	case IG_LEGEND:
	case IG_EPIC:
		{
			E_ITEM_GRADE const eNextItemGrade = GetNextGrade(eItemGrade);
			double dValue = 10 * pow(double(eNextItemGrade),2);
			dValue *= 1+(pItemDef->GetAbil(AT_LEVELLIMIT)/7);
			dValue *= GetRarityUpgradeCostRate(pItemDef->EquipPos());
			return std::max(1,static_cast<int>(dValue - (dValue*(pkUnit->GetAbil(AT_DEC_SOUL_RATE)+iAddDecSoulRate))/10000));
		}break;
	case IG_CURSE:		
		{
			return (pItemDef->GetAbil(AT_LEVELLIMIT) + (int)kItem.EnchantInfo().Rarity())/2;
		}break;
	}
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
	return 0;
}

__int64 const PgItemRarityUpgradeFormula::GetNeedEnchantCost(E_ITEM_GRADE const eItemGrade, PgBase_Item const & kItem)
{
	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
		return 0;
	}

	switch(eItemGrade)
	{
	case IG_NORMAL:
	case IG_RARE:
	case IG_UNIQUE:
	case IG_ARTIFACT:
	case IG_LEGEND:
	case IG_EPIC:
		{	
			return	static_cast<__int64>((eItemGrade+1) * pow(static_cast<double>(pItemDef->GetAbil(AT_LEVELLIMIT)),1.4) * 100 * GetRarityUpgradeCostRate(pItemDef->EquipPos()));
//			return 	static_cast<__int64>(pItemDef->GetAbil(AT_LEVELLIMIT) * pow(static_cast<double>(GetRarityUpgradeCostRate(pItemDef->EquipPos()) * (kItem.EnchantInfo().Rarity() + 10)),1.2)); // old
		}break;
	case IG_CURSE:		
		{
			return 	static_cast<__int64>(pItemDef->GetAbil(AT_LEVELLIMIT) * (kItem.EnchantInfo().Rarity() * 4) * GetRarityUpgradeCostRate(pItemDef->EquipPos()));
		}break;
	}
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
	return 0;
}

int const PgItemRarityUpgradeFormula::GetBrokenRate(E_ITEM_GRADE const eItemGrade)
{
	switch(eItemGrade)
	{
	case IG_CURSE:
	case IG_NORMAL:
	case IG_LEGEND:
	case IG_EPIC:
		{
			return 0;
		}break;
	case IG_RARE:
		{
			return 3000;
		}break;
	case IG_UNIQUE:
		{
			return 6000;
		}break;
	case IG_ARTIFACT:
		{
			return 9000;
		}break;
	}
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
	return 0;
}

__int64 const PgItemRarityUpgradeFormula::GetPlusUpgradeCost(PgBase_Item const & rkItem)
{
	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pDef = kItemDefMgr.GetDef(rkItem.ItemNo());
	if(!pDef)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
		return 0;
	}

	int const iItemLv = pDef->GetAbil(AT_LEVELLIMIT);

	return __int64(iItemLv * 100 * (rkItem.EnchantInfo().PlusLv() + 1) + (iItemLv*500) + (rkItem.EnchantInfo().PlusLv()*1000) * GetRarityUpgradeCostRate(pDef->EquipPos()));
}

SDefBasicOptionAmp const * PgItemRarityUpgradeFormula::GetBasicOptionAmp(EEquipType const eEquipType, int const iLimitLv, int const iAmpLv)
{
	CONT_DEFBASICOPTIONAMP const * pkDef = NULL;
	g_kTblDataMgr.GetContDef(pkDef);
	return GetBasicOptionAmp(eEquipType, iLimitLv, iAmpLv, pkDef);
}

SDefBasicOptionAmp const * PgItemRarityUpgradeFormula::GetBasicOptionAmp(EEquipType const eEquipType, int const iLimitLv, int const iAmpLv, CONT_DEFBASICOPTIONAMP const * pkDef)
{
	if(!pkDef)
	{
		return NULL;
	}

	CONT_DEFBASICOPTIONAMP::key_type const kKey(eEquipType, iLimitLv, iAmpLv);
	CONT_DEFBASICOPTIONAMP::const_iterator iter = pkDef->find(kKey);
	if(iter != pkDef->end())
	{
		return &(*iter).second;
	}
	return NULL;
}

void PgItemRarityUpgradeFormula::ApplyBasicOptionAmp(EEquipType const eEquipType, int const iLevel, int const iBasicAmpLv, CAbilObject & kOptBasicAbil, CONT_DEFBASICOPTIONAMP const * pkDefBasicOptionAmp)
{
	SDefBasicOptionAmp const * pkOptionAmpInfo = PgItemRarityUpgradeFormula::GetBasicOptionAmp(eEquipType, iLevel, iBasicAmpLv, pkDefBasicOptionAmp);
	if(pkOptionAmpInfo)
	{
		if(0 < pkOptionAmpInfo->iAmpRate)
		{
			SDefItemAmplify_Specific const* pkSpecific = PgItemRarityUpgradeFormula::ItemAmplifyRateSpecific(eEquipType);
			if(pkSpecific)
			{
				CAbilObject kTmpAbil;

				SAbilIterator kItor;
				kOptBasicAbil.FirstAbil(&kItor);
				while(kOptBasicAbil.NextAbil(&kItor))
				{
					int const iSpecificRate = pkSpecific->GetRate(kItor.wType);
					float fRate = pkOptionAmpInfo->iAmpRate;
					if(0 != iSpecificRate)
					{
						fRate = (pkOptionAmpInfo->iAmpRate * iSpecificRate) / ABILITY_RATE_VALUE;
					}
					kItor.iValue *= (ABILITY_RATE_VALUE + fRate) / ABILITY_RATE_VALUE_FLOAT;
					kTmpAbil.SetAbil(kItor.wType, kItor.iValue);
				}
				kOptBasicAbil.Swap(kTmpAbil);
			}
			else
			{
				kOptBasicAbil *= (ABILITY_RATE_VALUE + pkOptionAmpInfo->iAmpRate);
				kOptBasicAbil /= ABILITY_RATE_VALUE;
			}
		}
	}
}

int PgItemRarityUpgradeFormula::GetBasicOptionAmpRate(WORD const wType, EEquipType const eEquipType, int const iLevel, int const iBasicAmpLv)
{
	SDefBasicOptionAmp const * pkOptionAmpInfo = PgItemRarityUpgradeFormula::GetBasicOptionAmp(eEquipType, iLevel, iBasicAmpLv);
	if(pkOptionAmpInfo)
	{
		int iAmpValue = ABILITY_RATE_VALUE + pkOptionAmpInfo->iAmpRate;
		if(0 < iAmpValue)
		{
			SDefItemAmplify_Specific const* pkSpecific = PgItemRarityUpgradeFormula::ItemAmplifyRateSpecific(eEquipType);
			if(pkSpecific)
			{
				int const iSpecificRate = pkSpecific->GetRate(wType);
				float fRate = pkOptionAmpInfo->iAmpRate;
				if(0 != iSpecificRate)
				{
					fRate = (pkOptionAmpInfo->iAmpRate * iSpecificRate) / ABILITY_RATE_VALUE;
				}
				iAmpValue = ABILITY_RATE_VALUE + fRate;
			}
		}
		return iAmpValue;
	}
	return 0;
}

SDefItemAmplify_Specific const * PgItemRarityUpgradeFormula::ItemAmplifyRateSpecific(int const iEquipType)
{
	CONT_DEFITEM_AMP_SPECIFIC const * pkDef = NULL;
	g_kTblDataMgr.GetContDef(pkDef);
	return ItemAmplifyRateSpecific(iEquipType, pkDef);
}

SDefItemAmplify_Specific const * PgItemRarityUpgradeFormula::ItemAmplifyRateSpecific(int const iEquipType, CONT_DEFITEM_AMP_SPECIFIC const * pkDef)
{
	if(!pkDef)
	{
		return NULL;
	}

	CONT_DEFITEM_AMP_SPECIFIC::const_iterator iter = pkDef->find(iEquipType);
	if(iter != pkDef->end())
	{
		return &(*iter).second;
	}
	return NULL;
}

bool PgItemRarityUpgradeFormula::GetMaxGradeLevel( E_ITEM_GRADE const kItemGrade, bool const bIsPet, int &iOutLevel )
{
	if ( true == bIsPet )
	{
		switch(kItemGrade)
		{
		case IG_NORMAL:
		case IG_RARE:
		case IG_UNIQUE:
		case IG_LEGEND:
		case IG_EPIC:
			{
				iOutLevel = IPULL_LEGEND_LIMIT;
			}break;
		case IG_CURSE:
		case IG_SEAL:
		default:
			{
				return false;
			}break;
		}
	}
	else
	{
		switch(kItemGrade)
		{
		case IG_NORMAL:		{iOutLevel = IPULL_NORMAL_LIMIT;}break;
		case IG_RARE:		{iOutLevel = IPULL_RARE_LIMIT;}break;
		case IG_UNIQUE:		{iOutLevel = IPULL_UNIQUE_LIMIT;}break;
		case IG_ARTIFACT:	{iOutLevel = IPULL_ARTIFACT_LIMIT;}break;
		case IG_LEGEND:		{iOutLevel = IPULL_LEGEND_LIMIT;}break;
		case IG_EPIC:		{iOutLevel = IPULL_EPIC_LIMIT;}break;
		case IG_CURSE:
		case IG_SEAL:
		default:
			{// 렙업 불가능
				return false;
			}break;
		}
	}

	return true;
}



TBL_DEF_ITEMENCHANTSHIFT const * PgItemRarityUpgradeFormula::GetEnchantShiftPlusInfo(int const iItemNo, int const iLv, int const iTargetLv)
{
	CONT_DEF_ITEM_ENCHANT_SHIFT const *pCont = NULL;   
	g_kTblDataMgr.GetContDef(pCont);
	if(NULL == pCont)
	{
		return NULL;
	}

	EEquipType const eType = GetEquipType(iItemNo);
	CONT_DEF_ITEM_ENCHANT_SHIFT::key_type const kKey(static_cast<int>(eType),iLv,iTargetLv);
	CONT_DEF_ITEM_ENCHANT_SHIFT::const_iterator itr = pCont->find(kKey);
	if(itr != pCont->end())
	{
		return &(*itr).second;
	}
	
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return NULL"));
	return NULL;
}

int const DEFAULT_ENCHANT_BONUS_RATE = 5;

int	const PgItemRarityUpgradeFormula::GetEnchantBonusRate(short const siCount)
{
	int iResult = 0;
	for(short i = 0;i < siCount;++i)
	{
		iResult += (DEFAULT_ENCHANT_BONUS_RATE + i);
	}
	return iResult;
}