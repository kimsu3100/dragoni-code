#include "stdafx.h"
#include "lohengrin/variablecontainer.h"
#include "Variant/Global.h"
#include "Variant/Item.h"
#include "Variant/pgitemrarityupgradeformula.h"
#include "Variant/PgSocketFormula.h"
#include "Variant/PgLogUtil.h"
#include "constant.h"
#include "PgRecvFromUser.h"
#include "PgAction.h"
#include "PgRequest.h"
#include "PgActionAchievement.h"


int const JUNK_ITEM_NO = 79000000;

TBL_DEF_ITEMPLUSUPGRADE const *GetPlusInfo(int const iNextLv, int const iEquipPos, bool const bIsPet )//업그레이드 결과물(1레벨이 되기 위해서)
{
	SItemPlusUpgradeKey kKey( bIsPet, iEquipPos, iNextLv);//장착위치를 넣어야함.

	CONT_DEF_ITEM_PLUS_UPGRADE const *pCont = NULL;
	g_kTblDataMgr.GetContDef(pCont);

	CONT_DEF_ITEM_PLUS_UPGRADE::const_iterator itr = pCont->find(kKey);
	if( itr != pCont->end())
	{
		return &(itr->second);
	}
	
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return NULL"));
	return NULL;
}

TBL_DEF_ITEM_RARITY_UPGRADE const* GetRarityInfo(PgBase_Item const &kItem)//세공 결과물(1레벨이 되기 위해서)
{
	E_ITEM_GRADE const eItemGrade = ::GetItemGrade(kItem);

	const CONT_DEF_ITEM_RARITY_UPGRADE* pCont = NULL;
	g_kTblDataMgr.GetContDef(pCont);

	CONT_DEF_ITEM_RARITY_UPGRADE::const_iterator itor = pCont->find(eItemGrade);
	if(itor != pCont->end())
	{
		return &(*itor).second;
	}
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return NULL"));
	return NULL;
}

TBL_DEF_ITEMRAREGROUP const* GetRareGroup(int const iGroupNo)//레어 그룹 데이터 뽑기.
{
	const CONT_DEFITEMRAREGROUP* pCont = NULL;
	g_kTblDataMgr.GetContDef(pCont);
	CONT_DEFITEMRAREGROUP::const_iterator itor = pCont->find(iGroupNo);
	if(itor != pCont->end())
	{
		return &(*itor).second;
	}
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return NULL"));
	return NULL;
}

PgAction_ItemPlusUpgrade::PgAction_ItemPlusUpgrade(SGroundKey const &kGroundKey, BM::Stream & kPacket, int const iAddRate)
		:m_kGndKey(kGroundKey),m_kPacket(kPacket),m_kUseInsurance(false), m_iItemNo(0), m_iNowLevel(0), m_iNextLevel(0), m_iAddRate(iAddRate)
{
}
PgAction_ItemPlusUpgrade::~PgAction_ItemPlusUpgrade()
{
}
int const PgAction_ItemPlusUpgrade::GetRareType(int const iGroupNo, int const iRareIndex)//레어 그룹 데이터 뽑기.
{
	const TBL_DEF_ITEMRAREGROUP* pTbl = GetRareGroup(iGroupNo);//레어 그룹 데이터 뽑기.
	if(pTbl)
	{
		return pTbl->aRareNo[iRareIndex];
	}
	LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
	return 0;
}

TBL_DEF_ITEMPLUSUPGRADE const * PgAction_ItemPlusUpgrade::GetPlusInfo( PgBase_Item const &kTargetItem, int const iLv, EPlusItemUpgradeResult &rkOutRet )
{
	if ( kTargetItem.EnchantInfo().IsBinding() )
	{
		rkOutRet = PIUR_CAN_NOT_ENCHANT;
		return NULL;
	}

	rkOutRet = PIUR_CAN_NOT_ENCHANT;

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pDef = kItemDefMgr.GetDef( kTargetItem.ItemNo() );
	if( pDef )
	{
		if ( ICMET_Cant_Enchant & pDef->GetAbil(AT_ATTRIBUTE) )
		{
			rkOutRet = PIUR_CAN_NOT_ENCHANT;
			return NULL;
		}

		E_ITEM_GRADE const eItemGrade = ::GetItemGrade(kTargetItem);
		int iMaxLv = 0;
		if ( true == PgItemRarityUpgradeFormula::GetMaxGradeLevel( eItemGrade, pDef->IsPetItem(), iMaxLv ) )
		{
			if ( iLv > iMaxLv )
			{
				rkOutRet = PIUR_OVER_LEVELLIMIT;
			}
			else
			{
				int const EquipPos = pDef->GetAbil(AT_EQUIP_LIMIT);
				TBL_DEF_ITEMPLUSUPGRADE const * pPlusInfo = ::GetPlusInfo( iLv, EquipPos, pDef->IsPetItem() );//업그레이드 될 레벨
				if( pPlusInfo )
				{
					rkOutRet = PIUR_SUCCESS;
					return pPlusInfo;
				}
			}
		}
		else
		{
			rkOutRet = PIUR_CAN_NOT_ENCHANT;
		}
	}

	return NULL;
}

bool PgAction_ItemPlusUpgrade::GenPlusType(TBL_DEF_ITEMPLUSUPGRADE const * pPlusInfo,int & iResultType)
{
	if(NULL == pPlusInfo)
	{
		return false;
	}

	size_t iRetIndex = 0;
	if(RouletteRate(pPlusInfo->RareGroupSuccessRate, iRetIndex, MAX_ITEM_RARE_KIND_ARRAY))
	{
		iResultType = GetRareType(pPlusInfo->RareGroupNo, iRetIndex);
		return true;
	}

	return false;
}

int MAX_DOWNGRADE_RATE = 30;
int const ENCHANT_ACHIEVEMENT_START = 9;

EPlusItemUpgradeResult PgAction_ItemPlusUpgrade::Process( CUnit* pkCaster, CONT_PLAYER_MODIFY_ORDER & kOrder, CUnit * pkTarget, BM::Stream * pkPacket)
{//제련
	m_kPacket.Pop(m_kItemPos);
	m_kPacket.Pop(m_kInsuranceItemPos);
	m_kPacket.Pop(m_kBonusRateItemPos);
	short siBonusRateItemCount = 0;
	m_kPacket.Pop(siBonusRateItemCount);

	PgInventory *pInv = pkCaster->GetInven();

	PgBase_Item kItem;
	if(S_OK !=  pInv->GetItem(m_kItemPos, kItem))
	{
		return PIUR_NOT_FOUND_ITEM;
	}

	if( LOCAL_MGR::NC_JAPAN == g_kLocal.ServiceRegion() 
		&& CheckIsCashItem(kItem))
	{//일본의 경우 캐시 아이템은 인챈트 관련 작업 불가
		return PIUR_CAN_NOT_ENCHANT;
	}

	m_iItemNo = kItem.ItemNo();
	m_iNowLevel = static_cast<int>(kItem.EnchantInfo().PlusLv()); // Log에 기록하기 위해 맴버 변수에 기록
	m_iNextLevel = m_iNowLevel + 1;
		
	EPlusItemUpgradeResult kRet = PIUR_NOT_FOUND_ITEM;
	TBL_DEF_ITEMPLUSUPGRADE const * pPlusInfo = GetPlusInfo( kItem, m_iNextLevel, kRet );//업그레이드 될 레벨
	if( NULL == pPlusInfo)
	{
		return kRet;
	}

	eMyHomeSideJob const kSideJob = MSJ_ENCHANT;

	__int64 const i64NeedMoney = PgItemRarityUpgradeFormula::GetPlusUpgradeCost(kItem) * PgMyHomeFuncRate::GetCostRate(kSideJob, pkTarget);
	if(i64NeedMoney)
	{
		__int64 const iMoney = pkCaster->GetAbil64(AT_MONEY);
		if((iMoney < i64NeedMoney) || (0 > i64NeedMoney))
		{
			return PIUR_NOT_ENOUGH_MONEY;
		}

		SPMOD_Add_Money kDelData(-i64NeedMoney);//필요머니 빼기.
		SPMO kIMO(IMET_ADD_MONEY, pkCaster->GetID(), kDelData);
		kOrder.push_back(kIMO);

		if(pkTarget && UT_MYHOME == pkTarget->UnitType())
		{
			PgMyHome const * pkMyHome = dynamic_cast<PgMyHome const *>(pkTarget);
			if(pkMyHome && 0 < (pkMyHome->GetAbil(AT_HOME_SIDEJOB) & kSideJob))
			{
				SHOMEADDR const & kHomeAddr = pkMyHome->HomeAddr();
				SPMO kIMO(IMET_SIDEJOB_MODIFY, pkTarget->GetID(), SMOD_MyHome_SideJob_Modify(kHomeAddr.StreetNo(), kHomeAddr.HouseNo(), kSideJob, i64NeedMoney));
				kOrder.push_back(kIMO);
			}
		}
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);

	int iBonusRate = 0;
	int iNeedInsuranceItemCoun = 0;
	for( int i = 0;i < MAX_ITEM_PLUS_UPGRADE_NEED_ARRAY;++i)
	{
		int const iItemNo = pPlusInfo->aNeedItemNo[i];
		int const iNeedCount = pPlusInfo->aNeedItemCount[i];

		if( (0 != iItemNo) && (0 != iNeedCount))
		{
			switch(i)
			{
			case PUNT_NEED_ITEM:
				{
					CItemDef const * pItemDef = kItemDefMgr.GetDef(iItemNo);
					if(!pItemDef)
					{
						return PIUR_NOT_FOUND_ITEM;
					}

					int iTotalCount = pInv->GetTotalCount(iItemNo);

					int const iCustomType = pItemDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE);
					if(0 < iCustomType)
					{
						ContHaveItemNoCount kCont;
						pInv->GetItems(static_cast<EUseItemCustomType>(iCustomType), kCont);

						iTotalCount = 0;

						for(ContHaveItemNoCount::const_iterator iter = kCont.begin();iter != kCont.end();++iter)
						{
							iTotalCount += (*iter).second;
						}

						if(iNeedCount > iTotalCount)
						{
							return PIUR_NOT_ENOUGH_RES;
						}

						int iTotalDeleteCount = iNeedCount;

						for(ContHaveItemNoCount::const_iterator iter = kCont.begin();iter != kCont.end();++iter)
						{
							int const iDeleteCount = std::min<int>(iTotalDeleteCount, (*iter).second);
							if(0 >= iDeleteCount)
							{
								break;
							}
							iTotalDeleteCount -= iDeleteCount;
							kOrder.push_back(SPMO(IMET_ADD_ANY, pkCaster->GetID(), SPMOD_Add_Any((*iter).first, -iDeleteCount)));
						}
					}
					else
					{
						if(iNeedCount > iTotalCount)
						{
							return PIUR_NOT_ENOUGH_RES;
						}

						kOrder.push_back(SPMO(IMET_ADD_ANY, pkCaster->GetID(), SPMOD_Add_Any(iItemNo, -iNeedCount)));
					}
				}break;
			case PUNT_BONUS_RATE_ITEM:
				{
				}break;
			case PUNT_INSURANCE_ITEM:
				{
					if( 0 != iItemNo )
					{
						iNeedInsuranceItemCoun = iNeedCount;
					}
				}break;
			}
		}	
	}

	if( SItemPos::NullData() != m_kBonusRateItemPos )
	{
		PgBase_Item kBonusItem;
		if( S_OK != pInv->GetItem(m_kBonusRateItemPos,kBonusItem) )
		{
			return PIUR_NOT_FOUND_ITEM;
		}

		CItemDef const *pBounsItemDef = kItemDefMgr.GetDef(kBonusItem.ItemNo());
		if( !pBounsItemDef || 
			(UICT_PLUSE_SUCCESS != pBounsItemDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE)) ||
			(m_iNowLevel < pBounsItemDef->GetAbil(AT_USE_ITEM_CUSTOM_VALUE_1)) ||
			(pBounsItemDef->GetAbil(AT_USE_ITEM_CUSTOM_VALUE_2) < m_iNowLevel))
		{
			return PIUR_NOT_FOUND_ITEM;
		}

		short siUseCount = std::max<short>(siBonusRateItemCount,1);
		siUseCount = std::min<short>(siBonusRateItemCount,10);

		if(kBonusItem.Count() < siUseCount)
		{
			return PIUR_NOT_ENOUGH_RES;
		}

		iBonusRate = PgItemRarityUpgradeFormula::GetEnchantBonusRate(siUseCount);
		iBonusRate = ((MAX_ENCHANT_SUCCESS_RATE * iBonusRate)/100);
		kOrder.push_back(SPMO(IMET_MODIFY_COUNT, pkCaster->GetID(), SPMOD_Modify_Count(kBonusItem,m_kBonusRateItemPos,-siUseCount)));
	}

	if( SItemPos::NullData() != m_kInsuranceItemPos )
	{
		PgBase_Item kInsuranceItem;
		if( S_OK != pInv->GetItem(m_kInsuranceItemPos, kInsuranceItem) )
		{
			return PIUR_NOT_FOUND_ITEM;
		}

		GET_DEF(CItemDefMgr, kItemDefMgr);
		CItemDef const *pItemDef = kItemDefMgr.GetDef(kInsuranceItem.ItemNo());
		if(!pItemDef || (UICT_ENCHANT_INSURANCE != pItemDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE)) ||
			(m_iNowLevel < pItemDef->GetAbil(AT_USE_ITEM_CUSTOM_VALUE_1)) ||
			(pItemDef->GetAbil(AT_USE_ITEM_CUSTOM_VALUE_2) < m_iNowLevel))
		{
			return PIUR_NOT_FOUND_ITEM;
		}
		
		if( kInsuranceItem.Count() < iNeedInsuranceItemCoun )
		{
			return PIUR_NOT_ENOUGH_RES;
		}

		m_kUseInsurance = true;
		kOrder.push_back(SPMO(IMET_MODIFY_COUNT, pkCaster->GetID(), SPMOD_Modify_Count(kInsuranceItem,m_kInsuranceItemPos,-iNeedInsuranceItemCoun)));
	}

	int iPremiumAddEnchantRate = 0;
	PgPlayer* pkPlayer = dynamic_cast< PgPlayer* >(pkCaster);
	if( pkPlayer )
	{
		if( S_PST_AddEnchant const* pkPremiumAddEnchant = pkPlayer->GetPremium().GetType<S_PST_AddEnchant>() )
		{
			iPremiumAddEnchantRate = pkPremiumAddEnchant->iRate;
		}
	}

	int iSuccessrate = pPlusInfo->SuccessRate + iBonusRate + pkCaster->GetAbil(AT_ADD_ENCHANT_RATE) + m_iAddRate + PgMyHomeFuncRate::GetSuccessRate(kSideJob, pkTarget);
	iSuccessrate += SRateControl::GetValueRate<int>(iSuccessrate, iPremiumAddEnchantRate);
	int const iSuccessRandRet = BM::Rand_Index(MAX_ENCHANT_SUCCESS_RATE);
	bool const bSuccess = (iSuccessrate > iSuccessRandRet);
	bool bGodHand = false;
	if( g_kProcessCfg.RunMode() & CProcessConfig::E_RunMode_Debug )
	{
		PgPlayer* pkPlayer = dynamic_cast< PgPlayer* >(pkCaster);
		if( pkPlayer )
		{
			pkPlayer->SendWarnMessageStrDebug(BM::vstring(L"SuccessRate:") << iSuccessrate << L", RandRet:" << iSuccessRandRet);
		}
	}

	if( 1 == pkCaster->GetAbil(AT_GM_GODHAND) )
	{
		bGodHand = true;
	}

	// 공지는 인첸트 성공한 경우에만 한다.
	if(((true == bGodHand) || (true == bSuccess)) && abs(m_iNextLevel - IPULL_LIMIT_MAX) <= ITEM_PLUSE_UPGRADE_NOTI_LIMIT)
	{
		BM::Stream kBroadPacket( PT_N_C_NFY_NOTICE_PACKET, static_cast<size_t>(1) );
		kBroadPacket.Push(NOTICE_PLUSE_UPGRADE_ITEM);
		kBroadPacket.Push(pkCaster->Name());
		kBroadPacket.Push(kItem.ItemNo());
		kBroadPacket.Push(m_iNextLevel);
		kBroadPacket.Push(bSuccess);
		pkPacket->Push(true);
		pkPacket->Push(kBroadPacket.Data());
	}
	else
	{
		pkPacket->Push(false);
	}

	if(false == bSuccess && false == bGodHand )
	{
		PgAddAchievementValue kMA(AT_ACHIEVEMENT_ENCHANT_FAIL,1,m_kGndKey);
		kMA.DoAction(pkCaster,NULL);

		if(4 > m_iNowLevel )
		{
			return PIUR_NORMAL_FAILED;
		}

		if( 9 > m_iNowLevel)
		{	//인첸트 초기화. 고철 2개 지급

			SEnchantInfo kNewEnchantInfo = kItem.EnchantInfo();
			if(false == kNewEnchantInfo.HasEnchantFail())
			{
				kNewEnchantInfo.HasEnchantFail(true);
			}

			if(!m_kUseInsurance)
			{
				int const iPanaltyLv = 0;//제련 실패시 등급은 무조건 0.
				m_iNextLevel = iPanaltyLv;
				kNewEnchantInfo.PlusLv(m_iNextLevel);
			}

			if(kNewEnchantInfo != kItem.EnchantInfo())
			{
				SPMOD_Enchant kEnchantData( kItem, m_kItemPos, kNewEnchantInfo);//변경될 인첸트
				SPMO kIMO(IMET_MODIFY_ENCHANT, pkCaster->GetID(), kEnchantData);
				kOrder.push_back(kIMO);
			}

			if(kNewEnchantInfo.PlusLv() != kItem.EnchantInfo().PlusLv())
			{
				return PIUR_PANALTY_FAILED;//패널티 먹고 실패.
			}
			else
			{
				return PIUR_NORMAL_FAILED;
			}
		}
		else
		{
			if(!m_kUseInsurance)
			{//보험이면.
				SPMOD_Modify_Count kDelData(kItem, m_kItemPos, 1, true);//원본 깨기
				SPMO kIMO(IMET_MODIFY_COUNT, pkCaster->GetID(), kDelData);
				kOrder.push_back(kIMO);
				return PIUR_DESTROY_FAILED;//아이템 파괴 실패.
			}
			else
			{
				SEnchantInfo kNewEnchantInfo = kItem.EnchantInfo();
				if(false == kNewEnchantInfo.HasEnchantFail())
				{
					kNewEnchantInfo.HasEnchantFail(true);
				}

				if(IPULL_ARTIFACT_LIMIT < m_iNowLevel)	// 16 등급 부터 인첸트 보험을 사용해도 실패 하면 1 단계 등급이 하락한다.
				{
					int const iRand = BM::Rand_Range(100,1);
					if(MAX_DOWNGRADE_RATE >= iRand)
					{
						int const iPanaltyLv = -1;
						m_iNextLevel = m_iNowLevel + iPanaltyLv;
						kNewEnchantInfo.PlusLv(m_iNextLevel);
						SPMOD_Enchant kEnchantData( kItem, m_kItemPos, kNewEnchantInfo);//변경될 인첸트
						SPMO kIMO(IMET_MODIFY_ENCHANT, pkCaster->GetID(), kEnchantData);
						kOrder.push_back(kIMO);
						return PIUR_PANALTY_FAILED;//패널티 먹고 실패.
					}
				}

				if(kNewEnchantInfo != kItem.EnchantInfo())
				{
					SPMOD_Enchant kEnchantData( kItem, m_kItemPos, kNewEnchantInfo);//변경될 인첸트
					SPMO kIMO(IMET_MODIFY_ENCHANT, pkCaster->GetID(), kEnchantData);
					kOrder.push_back(kIMO);
				}

				return PIUR_NORMAL_FAILED;
			}
		}
	}

	SEnchantInfo kNewEnchantInfo = kItem.EnchantInfo();

	if(!m_iNowLevel)
	{//처음업글 //최초 속성 선택.

		int iResultType = 0;
		if(true == GenPlusType(pPlusInfo,iResultType))
		{
			kNewEnchantInfo.PlusType(iResultType);
		}
		else
		{
			return PIUR_NORMAL_FAILED;
		}
	}

	kNewEnchantInfo.PlusLv(m_iNextLevel);
							
	SPMOD_Enchant kEnchantData( kItem, m_kItemPos, kNewEnchantInfo);//변경될 인첸트

	SPMO kIMO(IMET_MODIFY_ENCHANT, pkCaster->GetID(), kEnchantData);
	kOrder.push_back(kIMO);

	switch(kNewEnchantInfo.PlusLv())
	{
	case 9:
		{
			PgAddAchievementValue kMA(AT_ACHIEVEMENT_ENCHANT9,1,m_kGndKey);
			kMA.DoAction(pkCaster,NULL);
		}break;
	case 11:
		{
			PgAddAchievementValue kMA(AT_ACHIEVEMENT_ENCHANT11,1,m_kGndKey);
			kMA.DoAction(pkCaster,NULL);
		}break;
	case 13:
		{
			PgAddAchievementValue kMA(AT_ACHIEVEMENT_ENCHANT13,1,m_kGndKey);
			kMA.DoAction(pkCaster,NULL);
		}break;
	case 15:
		{
			PgAddAchievementValue kMA(AT_ACHIEVEMENT_ENCHANT15,1,m_kGndKey);
			kMA.DoAction(pkCaster,NULL);
		}break;
	}

	PgAddAchievementValue kMA(AT_ACHIEVEMENT_ENCHANT_SUCCESS,1,m_kGndKey);
	kMA.DoAction(pkCaster,NULL);

	if((ENCHANT_ACHIEVEMENT_START <= kNewEnchantInfo.PlusLv()) && (false == kNewEnchantInfo.HasEnchantFail()))
	{
		int iAchievementType = (AT_ACHIEVEMENT_ENCHANT_NOFAIL9 + (kNewEnchantInfo.PlusLv() - ENCHANT_ACHIEVEMENT_START));
		if(AT_ACHIEVEMENT_ENCHANT_NOFAIL31 >= iAchievementType)
		{
			PgAddAchievementValue kMA(iAchievementType,1,m_kGndKey);
			kMA.DoAction(pkCaster,NULL);
		}
	}

	return PIUR_SUCCESS;
}

void PgAction_ItemPlusUpgrade::SendToAchievementMgr(PgPlayer * pPlayer)
{
	if( NULL == pPlayer )
	{
		return;
	}

	switch( g_kLocal.ServiceRegion() )
	{
	case LOCAL_MGR::NC_CHINA:
	case LOCAL_MGR::NC_DEVELOP:
		{
			if( ITEM_PLUSE_UPGRADE_NOTI_START <= m_iNextLevel )
			{// 10등급 이상 인챈트가 성공했을 때 모든 유저의 업적알리미에 표시한다.
				std::wstring ItemName;
				if( true == ::GetItemName(m_iItemNo, ItemName) )
				{
					BM::Stream Packet(PT_M_N_NFY_GENERIC_ACHIEVEMENT_NOTICE);
					Packet.Push(EAchievementType::E_ACHI_ENCHANT_SUCCESS);
					Packet.Push(pPlayer->Name());
					Packet.Push(m_iNextLevel);
					Packet.Push(ItemName);
					::SendToContents(Packet);
				}
			}
		}break;
	}
}

bool PgAction_ItemPlusUpgrade::DoAction(CUnit* pkCaster, CUnit* pkTarget)
{
	if(!pkCaster)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return false"));
		return false;
	}

	CONT_PLAYER_MODIFY_ORDER kOrder;

	BM::Stream kPacket(PT_C_M_ANS_ITEM_PLUS_UPGRADE);

	m_iNowLevel = 0;
	m_iNextLevel = 0;
	EPlusItemUpgradeResult const kErr = Process(pkCaster, kOrder, pkTarget, &kPacket);

	//제련
	PLUS_ITEM_UPGRADE_RESULT kResult;
	kResult.eResult = kErr;
	kPacket.Push(kResult);
	kPacket.Push(m_kUseInsurance);//보험 여부.

	switch ( kResult.eResult )
	{
	case PIUR_SUCCESS:
		{
			PgPlayer *pkPlayer = dynamic_cast<PgPlayer*>(pkCaster);
			if ( pkPlayer )
			{
				SPMOD_AddRankPoint kAddRank( E_RANKPOINT_ENCHANTSUCCEED, 1 );// 랭킹 올려.
				kOrder.push_back(SPMO(IMET_ADD_RANK_POINT, pkCaster->GetID(), kAddRank));

				SendToAchievementMgr(pkPlayer);
			}
		}break;
	case PIUR_NORMAL_FAILED:
	case PIUR_PANALTY_FAILED:
	case PIUR_DESTROY_FAILED:
		{
			PgPlayer *pkPlayer = dynamic_cast<PgPlayer*>(pkCaster);
			if ( pkPlayer )
			{
				SPMOD_AddRankPoint kAddRank( E_RANKPOINT_ENCHANTFAILED, 1 );// 랭킹 올려.
				kOrder.push_back(SPMO(IMET_ADD_RANK_POINT, pkCaster->GetID(), kAddRank));
			}
		}break;
	default:
		{
			// 여기로 들어온 에러는 모두 오류로 발생한것임 (클라로 보내자)
			pkCaster->Send(kPacket);
			return false;
		}break;
	}

	BM::Stream kWrappedPacket(PT_A_M_ADDON_WARPPED_PACKET); // 패킷을 한번 감싼다
	kWrappedPacket.Push( kPacket.Data() );
	kWrappedPacket.Push( m_iItemNo );
	kWrappedPacket.Push( kResult.eResult );
	kWrappedPacket.Push( m_iNowLevel );
	kWrappedPacket.Push( m_iNextLevel );

	PgAction_ReqModifyItem kAction(CIE_EnchantLvUp, m_kGndKey, kOrder, kWrappedPacket);
	kAction.DoAction(pkCaster, NULL);

	return true;
}

int GetFiveElementCrystalStone(int const iElement)
{
	CONT_FIVE_ELEMENT_INFO const * pContDefFiveElementInfo = NULL;
	g_kTblDataMgr.GetContDef(pContDefFiveElementInfo);
	
	CONT_FIVE_ELEMENT_INFO::const_iterator five_itor = pContDefFiveElementInfo->find(iElement);
	if(five_itor == pContDefFiveElementInfo->end())
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return 0"));
		return 0;
	}

	return (*five_itor).second.iCrystalStoneNo;
}

EItemRarityUpgradeResult PgAction_ItemRarityUpgrade::OnProcess(CUnit * pkCaster,CUnit * pkTarget)
{
	m_kPacket.Pop(m_kPropertyType);
	m_kPacket.Pop(m_kItemPos);

	m_kPacket.Pop(m_bUseInsuranceItem);
	m_kPacket.Pop(m_kInsuranceItemPos);

	m_kPacket.Pop(m_bUseSuccessRateItem);
	m_kPacket.Pop(m_kSuccessRateItemPos);

	CONT_PLAYER_MODIFY_ORDER kOrder;
	PgInventory *pInv = pkCaster->GetInven();

	PgPlayer * pkPlayer = dynamic_cast<PgPlayer *>(pkCaster);
	if(NULL == pkPlayer)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IRUR_NOT_FOUND_TARGET_ITEM"));
		return IRUR_NOT_FOUND_TARGET_ITEM;
	}

	PgBase_Item kItem;
	if(S_OK != pInv->GetItem(m_kItemPos, kItem))
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IRUR_NOT_FOUND_TARGET_ITEM"));
		return IRUR_NOT_FOUND_TARGET_ITEM;
	}

	E_ITEM_GRADE const eItemGrade = ::GetItemGrade(kItem);
	if(IG_SEAL == eItemGrade)
	{
		return IRUR_IS_SEALDING;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IRUR_NOT_FOUND_TARGET_ITEM"));
		return IRUR_NOT_FOUND_TARGET_ITEM;
	}

	if(!pItemDef->CanEquip() 
		|| (false == kItem.EnchantInfo().IsCurse() && (ICMET_Cant_SoulCraft & pItemDef->GetAbil(AT_ATTRIBUTE)))
		|| kItem.EnchantInfo().IsBinding())
	{
		return IRUR_NOT_ABLE_UPGRADE_ITEM;
	}

	TBL_DEF_ITEM_RARITY_UPGRADE const* pRarityInfo = GetRarityInfo(kItem);//업그레이드 될 레벨
	if((IG_CURSE != eItemGrade) && !pRarityInfo)
	{
		return IRUR_NOT_ABLE_UPGRADE_ITEM;
	}

	if( LOCAL_MGR::NC_JAPAN == g_kLocal.ServiceRegion() 
		&& CheckIsCashItem(kItem))
	{//일본의 경우 캐시 아이템은 소울 크래프트 관련 작업 불가
		return IRUR_NOT_ABLE_UPGRADE_ITEM;
	}

	int iExceptionRate = 0;//예외확률.
	int iBaseRate = 10000;//0이되면 안되니까. 초기화 십만.
	int iNeedSoulCrystal = 0;
	int iSuccessRate = 0;
	int iNeedFirstStoneNo = 0;
	int iNeedSecondStoneNo = 0;
	int iNeedFirstStoneCount = 0;
	int iNeedSecondStoneCount = 0;
	__int64 i64NeedCostCount = 0;

	int const iFiveElement = pkCaster->GetAbil(AT_FIVE_ELEMENT_TYPE_AT_BODY);

	if(m_kPropertyType)	// 속성 추가를 원하면 자기속성 크리스탈과 추가할 속성 크리스탈을 구해야함
	{
		if((pItemDef->EquipPos() != EQUIP_POS_SHIRTS) && (pItemDef->EquipPos() != EQUIP_POS_WEAPON))
		{
			return IRUR_NOT_ABLE_PROPERTY_ITEM;
		}

		if(E_PPTY_BASIC_MAX < m_kPropertyType)
		{
			return IRUR_INVALID_PROPERTY;
		}

		if(!PgItemRarityUpgradeFormula::GetNeedCrystalInfo(kItem,iFiveElement,iNeedFirstStoneNo,iNeedFirstStoneCount))
		{
			return IRUR_INVALID_PROPERTY;
		}

		if(!PgItemRarityUpgradeFormula::GetNeedCrystalInfo(kItem,m_kPropertyType,iNeedSecondStoneNo,iNeedSecondStoneCount,true))
		{
			return IRUR_INVALID_PROPERTY;
		}

		if(iNeedFirstStoneNo != iNeedSecondStoneNo)
		{
			if(((int)pInv->GetTotalCount(iNeedFirstStoneNo) < iNeedFirstStoneCount) ||
				((int)pInv->GetTotalCount(iNeedSecondStoneNo) < iNeedSecondStoneCount))
			{
				return IRUR_NOT_ENOUGH_CRYSTALSTONE;
			}

			SPMOD_Add_Any DelFirst(iNeedFirstStoneNo,-iNeedFirstStoneCount);
			SPMO MOF(IMET_ADD_ANY,pkCaster->GetID(),DelFirst);
			kOrder.push_back(MOF);

			SPMOD_Add_Any DelSecond(iNeedSecondStoneNo,-iNeedSecondStoneCount);
			SPMO MOS(IMET_ADD_ANY,pkCaster->GetID(),DelSecond);
			kOrder.push_back(MOS);
		}
		else
		{
			if((int)pInv->GetTotalCount(iNeedFirstStoneNo) < (iNeedFirstStoneCount + iNeedSecondStoneCount))
			{
				return IRUR_NOT_ENOUGH_CRYSTALSTONE;
			}

			SPMOD_Add_Any DelFirst(iNeedFirstStoneNo,-(iNeedFirstStoneCount + iNeedSecondStoneCount));
			SPMO MOF(IMET_ADD_ANY,pkCaster->GetID(),DelFirst);
			kOrder.push_back(MOF);
		}
	}
	else
	{
		// 영력 업그레이드는 레전드급 이상은 시도 할 수 없다.
		if(IG_EPIC <= eItemGrade)
		{
			return IRUR_NOT_ABLE_UPGRADE_ITEM;
		}

		if(true == m_bUseSuccessRateItem)	// 성공 확률 보정 아이템 사용 체크
		{
			PgBase_Item kSuccessRateItem;
			if(S_OK != pInv->GetItem(m_kSuccessRateItemPos, kSuccessRateItem))
			{
				return IRUR_NOT_ABLE_UPGRADE_ITEM;
			}

			CItemDef const * pSuccessRateItemDef = kItemDefMgr.GetDef(kSuccessRateItem.ItemNo());
			if(pSuccessRateItemDef && (UICT_RARITY_SUCCESS == pSuccessRateItemDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE)))
			{
				if(eItemGrade != pSuccessRateItemDef->GetAbil(AT_GRADE))
				{
					return IRUR_NOT_ABLE_UPGRADE_ITEM;
				}

				iSuccessRate = pSuccessRateItemDef->GetAbil(AT_SUCCESSRATE);

				SPMOD_Modify_Count ModifyCount(kSuccessRateItem,m_kSuccessRateItemPos,-1);
				SPMO MOF(IMET_MODIFY_COUNT,pkCaster->GetID(), ModifyCount);
				kOrder.push_back(MOF);

				m_bUseSucceRate = true;
			}
		}

		if(true == m_bUseInsuranceItem)
		{
			PgBase_Item kInsuranceItem;
			if(S_OK != pInv->GetItem(m_kInsuranceItemPos,kInsuranceItem))
			{
				return IRUR_NOT_ABLE_UPGRADE_ITEM;
			}

			CItemDef const * pInsuranceItemDef = kItemDefMgr.GetDef(kInsuranceItem.ItemNo());
			if(pInsuranceItemDef && (UICT_ENCHANT_INSURANCE == pInsuranceItemDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE)))
			{
				SPMOD_Modify_Count ModifyCount(kInsuranceItem,m_kInsuranceItemPos,-1);
				SPMO MOF(IMET_MODIFY_COUNT,pkCaster->GetID(),ModifyCount);
				kOrder.push_back(MOF);

				m_bUseInsurance = true;
			}
		}
	}

	int const iOldRarity = static_cast< int >(kItem.EnchantInfo().Rarity());

	eMyHomeSideJob const kSideJob = MSJ_SOULCRAFT;

	iNeedSoulCrystal = PgItemRarityUpgradeFormula::GetNeedSoulCount(eItemGrade ,kItem,pkCaster, m_iAddDecSoulRate) * PgMyHomeFuncRate::GetSoulRate(kSideJob, pkTarget);
	iExceptionRate = PgItemRarityUpgradeFormula::GetExceptionRate(eItemGrade,kItem) + iSuccessRate + PgMyHomeFuncRate::GetSuccessRate(kSideJob, pkTarget);
	i64NeedCostCount = PgItemRarityUpgradeFormula::GetNeedEnchantCost(eItemGrade,kItem) * PgMyHomeFuncRate::GetCostRate(kSideJob, pkTarget);

	__int64 const i64CurMoney = pInv->Money();
	if(i64CurMoney < i64NeedCostCount)
	{
		return IRUR_NOT_ENOUGH_MONEY;
	}

	if((int)pInv->GetTotalCount(ITEM_SOUL_NO) < iNeedSoulCrystal)
	{
		return IRUR_NOT_ENOUGH_SOUL;
	}

	SPMOD_Add_Any DelSoul(ITEM_SOUL_NO,-iNeedSoulCrystal);
	SPMO MODS(IMET_ADD_ANY,pkCaster->GetID(),DelSoul);
	kOrder.push_back(MODS);

	{//소울 사용개수에 따른 업적
		PgAddAchievementValue kMA( AT_ACHIEVEMENT_SOULEATER, iNeedSoulCrystal, m_kGndKey );
		kMA.DoAction( pkCaster, NULL );
	}

	SPMOD_Add_Money ModifyMoney(-i64NeedCostCount);
	SPMO MOM(IMET_ADD_MONEY,pkCaster->GetID(),ModifyMoney);
	kOrder.push_back(MOM);

	if(pkTarget && UT_MYHOME == pkTarget->UnitType())
	{
		PgMyHome const * pkMyHome = dynamic_cast<PgMyHome const *>(pkTarget);
		if(pkMyHome && 0 < (pkMyHome->GetAbil(AT_HOME_SIDEJOB) & kSideJob))
		{
			SHOMEADDR const & kHomeAddr = pkMyHome->HomeAddr();
			SPMO kIMO(IMET_SIDEJOB_MODIFY, pkTarget->GetID(), SMOD_MyHome_SideJob_Modify(kHomeAddr.StreetNo(), kHomeAddr.HouseNo(), kSideJob, i64NeedCostCount));
			kOrder.push_back(kIMO);
		}
	}

	iExceptionRate = static_cast<int>(iExceptionRate * (iNeedSecondStoneCount > 0 ? 1.2f : 1.0f)); // 두번째 속성석을 사용하면 속성 부여와 함께 성공확률도 1.2배 올라간다.
	iExceptionRate += pkCaster->GetAbil(AT_ADD_SOULCRAFT_RATE);	// 몬스터 카드 어빌 추가 성공 확률 증가.
	if( S_PST_SoulCraft const* pkPremiumSoulCraft = pkPlayer->GetPremium().GetType<S_PST_SoulCraft>() )
	{
		iExceptionRate += SRateControl::GetValueRate<int>(iExceptionRate, pkPremiumSoulCraft->iRate);
	}

	EItemRarityUpgradeResult kResult = IRUR_SUCCESS;

	PgBase_Item kItemCopy = kItem;

	bool bGodHand = false;
	if( 1 == pkCaster->GetAbil( AT_GM_GODHAND ) )
	{//소울 100% 성공
		bGodHand = true;
	}

	//10000을 넘게 셋팅, 그 아랫자리로 확률돌림.
	if( lwIsRandSuccess(iExceptionRate) || bGodHand )
	{
		SEnchantInfo kNewEnchantInfo = kItemCopy.EnchantInfo();

		switch(eItemGrade)
		{
		case IG_NORMAL:		
		case IG_RARE:		
		case IG_UNIQUE:		
		case IG_ARTIFACT:		// 이등급까지는 다음 등급으로 증가가 가능
		case IG_LEGEND:
			{
				short nRet = 0;
				if(S_OK == GenRarityValue((E_ITEM_GRADE const)(eItemGrade+1), nRet))
				{
					kNewEnchantInfo.Rarity( nRet );//새로운 등급 적용
				}
			}			// 여기서 리턴 하면 안됨... 아래쪽은 공통 처리임
		case IG_EPIC:	// 최종 등급이다. 더 이상 증가 없다.
			{
				if(m_kPropertyType)
				{
					if(m_kPropertyType == kNewEnchantInfo.Attr())
					{
						__int64 i64Lv = kNewEnchantInfo.AttrLv() + 1;
						i64Lv = __min(PROPERTY_LEVEL_LIMIT,i64Lv);
						kNewEnchantInfo.AttrLv(i64Lv);
					}
					else
					{
						kNewEnchantInfo.AttrLv(1);
					}
					kNewEnchantInfo.Attr(m_kPropertyType);	// 속성 추가/변경
				}

				kItemCopy.EnchantInfo(kNewEnchantInfo);

				E_ITEM_GRADE const eCurItemGrade = ::GetItemGrade(kItemCopy);

				GET_DEF(PgItemOptionMgr, kItemOptionMgr);
				if(eItemGrade != eCurItemGrade)
				{
					kItemOptionMgr.ReDiceOption(kItemCopy);//AddOption
				}
			}break;
		case IG_CURSE:		
			{
				kNewEnchantInfo.IsCurse(false);
				kItemCopy.EnchantInfo(kNewEnchantInfo);
			}break;
		}

		SPMOD_Enchant kEnchantData( kItem, m_kItemPos, kItemCopy.EnchantInfo());//변경될 인첸트
		SPMO kIMO(IMET_MODIFY_ENCHANT, pkCaster->GetID(), kEnchantData);
		kOrder.push_back(kIMO);

		E_ITEM_GRADE const eCurItemGrade = ::GetItemGrade(kItemCopy);

		switch(eCurItemGrade)//현재 등급
		{
		case IG_UNIQUE:
			{//고급
				PgAddAchievementValue kMA(AT_ACHIEVEMENT_SOULCRAFT3,1,m_kGndKey);
				kMA.DoAction(pkCaster,NULL);
			}break;
		case IG_ARTIFACT:
			{//스페셜
				PgAddAchievementValue kMA(AT_ACHIEVEMENT_SOULCRAFT4,1,m_kGndKey);
				kMA.DoAction(pkCaster,NULL);
			}break;
		}
	}
	else
	{
		int iBrokenRate = PgItemRarityUpgradeFormula::GetBrokenRate(eItemGrade);
		if( !m_bUseInsurance//보험 아니고.
		&&	iBrokenRate > 0 
		&& lwIsRandSuccess(iBrokenRate))
		{
			ContItemRemoveOrder kContOrder;//깨지는거 좋은데.. 비지 않았으면.
			SItemRemoveOrder kElement;
			kElement.kCasterPos = m_kItemPos;
			kContOrder.push_back(kElement);
		
			PgAction_ReqRemoveInvItem kAction(m_kGndKey, kContOrder, IRT_UPGRADE_REMOVE);
			kAction.DoAction(pkCaster, pkTarget);

			kResult = IRUR_FAIL_AND_BROKEN;
		}
		else
		{
			kResult = IRUR_FAIL;
		}
	}
	E_ITEM_GRADE const eNewItemGrade = ::GetItemGrade(kItemCopy);

	BM::Stream kPacket(PT_M_C_ANS_ITEM_RARITY_UPGRADE, kResult);
	kPacket.Push(m_bUseInsurance);// 보험 아이템 사용 여부 전송
	kItemCopy.WriteToPacket(kPacket);

	BM::Stream kWrappedPacket(PT_A_M_ADDON_WARPPED_PACKET); // 패킷을 한번 감싼다
	kWrappedPacket.Push( kPacket.Data() );
	kWrappedPacket.Push( kItem.ItemNo() );
	kWrappedPacket.Push( kResult );
	kWrappedPacket.Push( eItemGrade );
	kWrappedPacket.Push( eNewItemGrade );

	PgAction_ReqModifyItem kAction(CIE_SoulCraft, m_kGndKey, kOrder, kWrappedPacket);
	kAction.DoAction(pkCaster, NULL);

	return kResult;
}

bool PgAction_ItemRarityUpgrade::DoAction(CUnit* pkCaster, CUnit* pkTarget)
{
	EItemRarityUpgradeResult const kResult = OnProcess(pkCaster,pkTarget);

	switch(kResult)
	{
	case IRUR_FAIL:
	case IRUR_FAIL_AND_BROKEN:
	case IRUR_SUCCESS:
		{
			return true;
		}break;
	}

	BM::Stream kPacket(PT_M_C_ANS_ITEM_RARITY_UPGRADE, kResult);
	kPacket.Push(m_bUseInsurance);// 보험 아이템 사용 여부 전송
	pkCaster->Send(kPacket);
	return true;
}

EItemDischargeResult PgAction_ItemDischarge::OnProcess(CUnit *pkCaster, CUnit *pkTarget)
{
	PgInventory *pInv = pkCaster->GetInven();

	PgBase_Item kItem;
	if(S_OK != pInv->GetItem(m_kItemPos, kItem))
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IDR_NOT_FOUNd_TARGET_ITEM"));
		return IDR_NOT_FOUND_TARGET_ITEM;
	}

	if(!kItem.EnchantInfo().IsSeal())
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IDR_NOT_SEALD_ITEM"));
		return IDR_NOT_SEALD_ITEM;
	}

	PgBase_Item kDischargeItem;
	if(S_OK != pInv->GetItem(m_kDischargeItemPos, kDischargeItem))
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IDR_NOT_FOUND_DISCHARGE_ITEM"));
		return IDR_NOT_FOUND_DISCHARGE_ITEM;
	}

	PgBase_Item kItemCopy = kItem;

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kDischargeItem.ItemNo());
	if(!pItemDef)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IDR_NOT_FOUND_DISCHARGE_ITEM"));
		return IDR_NOT_FOUND_DISCHARGE_ITEM;
	}

	int const iCustomType = pItemDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE);
	int const iCustomValue1 = pItemDef->GetAbil(AT_USE_ITEM_CUSTOM_VALUE_1);

	if(UICT_SEAL_REMOVE != iCustomType)
	{
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IDR_NOT_FOUND_DISCHARGE_ITEM"));
		return IDR_NOT_FOUND_DISCHARGE_ITEM;
	}

	SEnchantInfo kNewEnchantInfo = kItemCopy.EnchantInfo();

	if(0 < iCustomValue1)
	{
		PgBase_Item kTmpItem;
		HRESULT const kRes = CreateSItem(kItemCopy.ItemNo(),1,iCustomValue1,kTmpItem);
		if(S_OK != kRes)
		{
			LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return IDR_NOT_FOUND_DISCHARGE_ITEM"));
			return IDR_NOT_FOUND_DISCHARGE_ITEM;
		}
		
		SEnchantInfo const & kTmpEnchant = kTmpItem.EnchantInfo();
		kNewEnchantInfo.Rarity(kTmpEnchant.Rarity());
		kNewEnchantInfo.IsCurse(false);
	}

	kNewEnchantInfo.IsSeal(false);
	kItemCopy.EnchantInfo(kNewEnchantInfo);

	GET_DEF(PgItemOptionMgr, kItemOptionMgr);

	kItemOptionMgr.GenerateOption_Sub(kItemCopy,pItemDef->GetAbil(AT_USE_ITEM_CUSTOM_VALUE_2));

	SPMOD_Enchant kEnchantData( kItem, m_kItemPos, kItemCopy.EnchantInfo());//변경될 인첸트
	SPMO kIMO(IMET_MODIFY_ENCHANT, pkCaster->GetID(), kEnchantData);
	m_kOrder.push_back(kIMO);

	PgPlayer* pkPlayer = dynamic_cast< PgPlayer* >(pkCaster);
	if( pkPlayer )
	{
		PgLogUtil::PgLogWrapperPlayer kLogCont(ELogMain_Contents_Item, ELogSub_Item_Enchent, *pkPlayer, m_kGndKey.GroundNo());
		PgLog kLog(ELOrderMain_Item_SoulCraft, ELOrderSub_None);
		kLog.Set( PgLogUtil::AtIndex(1), static_cast< int >(kItem.ItemNo()) );
		kLog.Set( PgLogUtil::AtIndex(2), static_cast< int >(IDR_SUCCESS) );
		kLog.Set( PgLogUtil::AtIndex(3), static_cast< int >(GetItemGrade(kItem)) );
		kLog.Set( PgLogUtil::AtIndex(4), static_cast< int >(GetItemGrade(kItemCopy)) );
		kLogCont.Add(kLog);
		kLogCont.Commit();
	}

	return IDR_SUCCESS;
}

bool PgAction_ItemDischarge::DoAction(CUnit *pkCaster, CUnit *pkTarget)
{
	EItemDischargeResult kResult = OnProcess(pkCaster,pkTarget);
	BM::Stream kPacket(PT_M_C_ANS_ITEM_DISCHARGE, kResult);
	pkCaster->Send(kPacket);
	return (IDR_SUCCESS == kResult);
}

HRESULT PgAction_ReqGenSocket::Process(CUnit * pUser, CUnit * pkTarget)
{
	if(!pUser)
	{
		return E_GS_SYSTEM_ERROR;
	}
	SItemPos kItemPos;
	m_kPacket.Pop(kItemPos);

	SItemPos kSuccessRatePos;
	m_kPacket.Pop(kSuccessRatePos);

	int iSocket_Order = 0;
	m_kPacket.Pop(iSocket_Order);

	PgBase_Item kItem;

	PgInventory * pkInv = pUser->GetInven();

	if(S_OK != pkInv->GetItem(kItemPos,kItem))
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	int iGenSocketState = 0;
	switch( iSocket_Order )
	{
	case PgSocketFormula::SII_FIRST:
		{
			iGenSocketState = kItem.EnchantInfo().GenSocketState();
		}break;
	case PgSocketFormula::SII_SECOND:
		{
			iGenSocketState = kItem.EnchantInfo().GenSocketState2();
		}break;
	case PgSocketFormula::SII_THIRD:
		{
			iGenSocketState = kItem.EnchantInfo().GenSocketState3();
		}break;
	default:
		{
			return E_GS_INVALID_IDX;
		}break;
	}

	if(GSS_GEN_NONE != iGenSocketState)
	{
		return E_GS_ALREADY_GEN;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	CONT_PLAYER_MODIFY_ORDER kOrder;

	int iAddSuccessRate = 0;
	if(SItemPos::NullData() != kSuccessRatePos)
	{
		PgBase_Item kSuccessItem;
		if(S_OK != pkInv->GetItem(kSuccessRatePos,kSuccessItem))
		{
			return E_GS_NOT_FOUND_ITEM;
		}

		CItemDef const *pSuccessDef = kItemDefMgr.GetDef(kSuccessItem.ItemNo());
		if(!pSuccessDef)
		{
			return E_GS_NOT_FOUND_ITEM;
		}

		if(UICT_SOCKET_SUCCESS != pSuccessDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE))
		{
			return E_GS_NOT_FOUND_ITEM;
		}

		iAddSuccessRate = pSuccessDef->GetAbil(AT_USE_ITEM_CUSTOM_VALUE_1);
		SPMOD_Modify_Count ModifyCount(kSuccessItem,kSuccessRatePos,-1);
		SPMO MOF(IMET_MODIFY_COUNT,pUser->GetID(),ModifyCount);
		kOrder.push_back(MOF);
	}

	E_ITEM_GRADE const kItemGrade = GetItemGrade(kItem);

	SEnchantInfo kEnchant = kItem.EnchantInfo();

	if( (ICMET_Cant_GenSocket == (pItemDef->GetAbil(AT_ATTRIBUTE) & ICMET_Cant_GenSocket)) ||
		(false == pItemDef->CanEquip()) /*|| (false == kEnchant.EanbleGenSocket(kItemGrade))*/)
	{
		return E_GS_CANNOT_GEN;
	}

	eMyHomeSideJob const kSideJob = MSJ_SOCKET;

	__int64 const i64Cost = PgSocketFormula::GetCreateNeedCost(kItem, iSocket_Order) * PgMyHomeFuncRate::GetCostRate(kSideJob, pkTarget);

	if(i64Cost > pUser->GetAbil64(AT_MONEY))
	{
		return E_GS_NOT_ENOUGH_MONEY;
	}

	int const iSoulCount = PgSocketFormula::GetCreateNeedSoul(kItem, iSocket_Order) * PgMyHomeFuncRate::GetSoulRate(kSideJob, pkTarget);

	size_t const kHasSoulCount = pkInv->GetTotalCount(ITEM_SOUL_NO);

	if(iSoulCount > static_cast<int>(kHasSoulCount))
	{
		return E_GS_NOT_ENOUGH_SOUL;
	}

	SPMOD_Add_Any DelSoul(ITEM_SOUL_NO,-iSoulCount);
	SPMO MODS(IMET_ADD_ANY,pUser->GetID(),DelSoul);
	kOrder.push_back(MODS);

	{//소울 사용개수에 따른 업적
		PgAddAchievementValue kMA( AT_ACHIEVEMENT_SOULEATER, iSoulCount, m_kGndKey );
		kMA.DoAction( pUser, NULL );
	}

	SPMOD_Add_Money ModifyMoney(-i64Cost);
	SPMO MOM(IMET_ADD_MONEY,pUser->GetID(),ModifyMoney);
	kOrder.push_back(MOM);


	if(pkTarget && UT_MYHOME == pkTarget->UnitType())
	{
		PgMyHome const * pkMyHome = dynamic_cast<PgMyHome const *>(pkTarget);
		if(pkMyHome && 0 < (pkMyHome->GetAbil(AT_HOME_SIDEJOB) & kSideJob))
		{
			SHOMEADDR const & kHomeAddr = pkMyHome->HomeAddr();
			SPMO kIMO(IMET_SIDEJOB_MODIFY, pkTarget->GetID(), SMOD_MyHome_SideJob_Modify(kHomeAddr.StreetNo(), kHomeAddr.HouseNo(), kSideJob, i64Cost));
			kOrder.push_back(kIMO);
		}
	}

	int iValue = 0;
	/*if( S_OK != g_kVariableContainer.Get(EVar_Kind_Socket, EVar_Socket_Generate_SuccessRate, iValue) )
	{
		VERIFY_INFO_LOG(false, BM::LOG_LV1, __FL__<<L"Can't Find 'EVar_Socket_Generate_SuccessRate'");
		LIVE_CHECK_LOG(BM::LOG_LV1, __FL__ << _T("Return false"));
		return E_GS_SYSTEM_ERROR;
	}*/
	iValue = PgSocketFormula::GetCreateSocketRate(kItem, iSocket_Order);

	int const iSuccessRate = iValue + iAddSuccessRate + PgMyHomeFuncRate::GetSuccessRate(kSideJob, pkTarget);

	kEnchant.IsMCTimeOuted(0);

	int iGenSocketValue = GSS_GEN_NONE;
	if(true == lwIsRandSuccess(iSuccessRate)
		|| pUser->GetAbil(AT_GM_GODHAND)
		)
	{
		iGenSocketValue = GSS_GEN_SUCCESS;
	}
	else
	{
		iGenSocketValue = GSS_GEN_FAIL;
	}

	switch( iSocket_Order )
	{
	case PgSocketFormula::SII_FIRST:
		{
			kEnchant.GenSocketState(iGenSocketValue);			
		}break;
	case PgSocketFormula::SII_SECOND:
		{
			kEnchant.GenSocketState2(iGenSocketValue);
		}break;
	case PgSocketFormula::SII_THIRD:
		{
			kEnchant.GenSocketState3(iGenSocketValue);
		}break;
	default:
		{
			return E_GS_INVALID_IDX;
		}break;
	}

	SPMOD_Enchant kEnchantData( kItem, kItemPos, kEnchant);//변경될 인첸트
	SPMO kIMO(IMET_MODIFY_ENCHANT,pUser->GetID(), kEnchantData);
	kOrder.push_back(kIMO);

	BM::Stream kPacket(PT_M_C_ANS_GEN_SOCKET);
	kPacket.Push(static_cast<bool>(GSS_GEN_SUCCESS == iGenSocketValue));
	PgAction_ReqModifyItem kAction(CIE_Gen_Socket,m_kGndKey, kOrder,kPacket);
	kAction.DoAction(pUser, NULL);

	return S_OK;
}

bool PgAction_ReqGenSocket::DoAction(CUnit * pUser,CUnit * pkTarget)
{
	HRESULT const kRet = Process(pUser, pkTarget);
	if(S_OK == kRet)
	{
		return true;
	}

	BM::Stream kPacket(PT_M_C_ANS_GEN_SOCKET);
	kPacket.Push(kRet);
	pUser->Send(kPacket);
	return true;
}

HRESULT PgAction_ReqSetMonsterCard::Process(CUnit * pUser)
{
	if(!pUser)
	{
		return E_GS_SYSTEM_ERROR;
	}
	SItemPos kCardPos;
	m_kPacket.Pop(kCardPos);

	SItemPos kItemPos;
	m_kPacket.Pop(kItemPos);
	
	int iInsuranceItemNo = 0;
	m_kPacket.Pop(iInsuranceItemNo);

	int iInsuranceItemCount = 0;
	m_kPacket.Pop(iInsuranceItemCount);

	PgBase_Item kItem,kCard;
	PgInventory * pkInv = pUser->GetInven();
	if( NULL == pkInv )
	{
		return E_GS_NOT_FOUND_ITEM;
	}
	if( (S_OK != pkInv->GetItem(kCardPos,kCard)) || 
		(S_OK != pkInv->GetItem(kItemPos,kItem)))
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	CItemDef const *pCardDef = kItemDefMgr.GetDef(kCard.ItemNo());
	if(!pCardDef)
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	if(UICT_MONSTERCARD != pCardDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE))
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	if(0 == (pItemDef->GetAbil(AT_EQUIP_LIMIT) & pCardDef->GetAbil(AT_EQUIP_LIMIT)))
	{
		return E_GS_CANNOT_SET;
	}

	int const iCardLevelMin = pCardDef->GetAbil(AT_LEVELLIMIT);
	int const iCardLevelMax = pCardDef->GetAbil(AT_MAX_LEVELLIMIT);

	if ((iCardLevelMin > pItemDef->GetAbil(AT_LEVELLIMIT)) || iCardLevelMax < pItemDef->GetAbil(AT_LEVELLIMIT))
	{
		return E_GS_CANNOT_SET;
	}


	E_ITEM_GRADE const kItemGrade = GetItemGrade(kItem);

	SEnchantInfo kEnchant = kItem.EnchantInfo();

	int iSelectIndex = pCardDef->GetAbil(AT_MONSTER_CARD_ORDER);
	int iGenSocketState = 0;
	int iGenSocketCard = 0;	

	switch( iSelectIndex )
	{
	case PgSocketFormula::SII_FIRST:
		{
			iGenSocketState = kEnchant.GenSocketState();
			iGenSocketCard = kEnchant.MonsterCard();
		}break;
	case PgSocketFormula::SII_SECOND:
		{
			iGenSocketState = kEnchant.GenSocketState2();
			iGenSocketCard = kEnchant.MonsterCard2();
		}break;
	case PgSocketFormula::SII_THIRD:
		{
			iGenSocketState = kEnchant.GenSocketState3();
			iGenSocketCard = kEnchant.MonsterCard3();
		}break;
	default:
		{
			return E_GS_NOT_FOUND_ITEM;
		}break;
	}

	if(GSS_GEN_SUCCESS != iGenSocketState)
	{
		return E_GS_NOT_GEN;
	}

	if(0 < iGenSocketCard)
	{
		return E_GS_CARD_FULL;
	}

	int const iCardNo = pCardDef->GetAbil(AT_MONSTER_CARD_NO);
	if(MONSTER_CARD_MAX_IDX < iCardNo)
	{
		return E_GS_OVER_MAX_IDX;
	}

	int iDefUseInsuranceItemNo = pCardDef->GetAbil(AT_USE_INSURANCE_ITEM_NO);
	size_t uiHaveInsuranceCount = pkInv->GetInvTotalCount( iInsuranceItemNo );
	if( uiHaveInsuranceCount < iInsuranceItemCount 
		|| 0 == iDefUseInsuranceItemNo 
		|| iDefUseInsuranceItemNo != iInsuranceItemNo)
	{// Def에 보험아이템 정보가 없거나, Def와 전송 정보가 맞지 않거나, 수량이 맞지 않을 경우
		return E_GS_NOT_FOUND_ITEM;
	}

	int ATSuccessRate = pCardDef->GetAbil(AT_SOCKETCARD_INSERT);
	int iSuccessRate = ATSuccessRate ? ATSuccessRate : 7000;
	bool bIsSuccess = false;
	bool const bUseInsurance = ( 0 < iInsuranceItemCount );
	if( 0 != iInsuranceItemCount )
	{
		int const iAddSuccessRate = (iInsuranceItemCount-1) * 3000;
		iSuccessRate += iAddSuccessRate;
	}

	CONT_PLAYER_MODIFY_ORDER kOrder;

	if(lwIsRandSuccess(iSuccessRate)
		|| pUser->GetAbil(AT_GM_GODHAND)
		)
	{
		bIsSuccess = true;

		switch( iSelectIndex )
		{
		case PgSocketFormula::SII_FIRST:
			{
				kEnchant.MonsterCard(iCardNo);
			}break;
		case PgSocketFormula::SII_SECOND:
			{
				kEnchant.MonsterCard2(iCardNo);
			}break;
		case PgSocketFormula::SII_THIRD:
			{
				kEnchant.MonsterCard3(iCardNo);		
			}break;
		default:
			{
			}break;
		}

		//kEnchant.MonsterCard(iCardNo);
		kEnchant.IsMCTimeOuted(0);

		int const iUseTime = pCardDef->GetAbil(AT_ITEM_OPTION_TIME);
		if(0 < iUseTime)
		{// 기간제 카드의 경우 기간제 정보를 저장한다.
			kOrder.push_back(SPMO(IMET_MODIFY_EXTEND_DATA,pUser->GetID(),SPMOD_ExtendData(kItem, kItemPos,SMonsterCardTimeLimit(iUseTime))));
		}

		SPMOD_Enchant kEnchantData( kItem, kItemPos, kEnchant);//변경될 인첸트
		SPMO kIMO(IMET_MODIFY_ENCHANT,pUser->GetID(), kEnchantData);
		kOrder.push_back(kIMO);
	}
	if( true == bIsSuccess
		|| false == bUseInsurance )
	{
		SPMOD_Modify_Count ModifyCount(kCard,kCardPos,-1);
		SPMO MOF(IMET_MODIFY_COUNT,pUser->GetID(),ModifyCount);
		kOrder.push_back(MOF);
	}
	else if( false == bIsSuccess 
		&& true == bUseInsurance )
	{
		pUser->SendWarnMessage(1209, EL_Normal );
	}
	if( 0 < iInsuranceItemCount )
	{
		SItemPos kItemPos;
		pkInv->GetFirstItem( iInsuranceItemNo, kItemPos, false);
		PgBase_Item kItem;
		if( S_OK != pkInv->GetItem( kItemPos, kItem ) )
		{
			return E_GS_NOT_FOUND_ITEM;
		}
		SPMOD_Modify_Count ModifyCount(kItem, kItemPos, -iInsuranceItemCount);
		SPMO MOF(IMET_MODIFY_COUNT, pUser->GetID(), ModifyCount);
		kOrder.push_back(MOF);
	}

	BM::Stream kPacket(PT_M_C_ANS_SET_MONSTERCARD);
	kPacket.Push(bIsSuccess);
	kPacket.Push(kCard.ItemNo());
	PgAction_ReqModifyItem kAction(CIE_Set_MonsterCard,m_kGndKey, kOrder,kPacket);
	kAction.DoAction(pUser, NULL);
	return S_OK;
}

bool PgAction_ReqSetMonsterCard::DoAction(CUnit * pUser,CUnit *)
{
	HRESULT const kRet = Process(pUser);
	if(S_OK == kRet)
	{
		return true;
	}

	BM::Stream kPacket(PT_M_C_ANS_SET_MONSTERCARD);
	kPacket.Push(kRet);
	pUser->Send(kPacket);
	return true;
}

bool PgAction_ReqRemoveSocket::DoAction(CUnit * pUser,CUnit * pTarget)
{
	HRESULT const kRet = Process(pUser);
	if(S_OK == kRet)
	{
		return true;
	}

	BM::Stream kPacket(PT_M_C_ANS_REMOVE_MONSTERCARD);
	kPacket.Push(kRet);
	pUser->Send(kPacket);
	return true;
}

HRESULT PgAction_ReqRemoveSocket::Process(CUnit * pUser)
{
	SItemPos kCardPos;
	SItemPos kItemPos;

	m_kPacket.Pop(kCardPos);
	m_kPacket.Pop(kItemPos);

	int iSocket_Order = 0;
	m_kPacket.Pop(iSocket_Order);

	PgBase_Item kItem,kCard;

	PgInventory * pkInv = pUser->GetInven();

	if( (S_OK != pkInv->GetItem(kCardPos,kCard)) || 
		(S_OK != pkInv->GetItem(kItemPos,kItem)))
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	CItemDef const *pCardDef = kItemDefMgr.GetDef(kCard.ItemNo());
	if(!pCardDef)
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	if(UICT_REMOVE_SOCKET != pCardDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE))
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	E_ITEM_GRADE const kItemGrade = GetItemGrade(kItem);

	SEnchantInfo kEnchant = kItem.EnchantInfo();

	int iRemoveSocketIndex = GSS_GEN_NONE;
	int iRemoveSocketCard = 0;

	switch( iSocket_Order )
	{
	case PgSocketFormula::SII_FIRST:
		{
			iRemoveSocketIndex = kEnchant.GenSocketState();
			iRemoveSocketCard = kEnchant.MonsterCard();
		}break;
	case PgSocketFormula::SII_SECOND:
		{
			iRemoveSocketIndex = kEnchant.GenSocketState2();
			iRemoveSocketCard = kEnchant.MonsterCard2();
		}break;
	case PgSocketFormula::SII_THIRD:
		{
			iRemoveSocketIndex = kEnchant.GenSocketState3();
			iRemoveSocketCard = kEnchant.MonsterCard3();
		}break;
	default:
		{
			return E_GS_INVALID_IDX;
		}break;
	}

	if(GSS_GEN_NONE == iRemoveSocketIndex)
	{
		return E_GS_NOT_GEN;
	}

	if(GSS_GEN_SUCCESS == iRemoveSocketIndex && 0 == iRemoveSocketCard)
	{
		return E_GS_NOT_SET_CARD;
	}

	__int64 const i64Cost = PgSocketFormula::GetRemoveNeedCost(kItem, iSocket_Order);

	if(i64Cost > pUser->GetAbil64(AT_MONEY))
	{
		return E_GS_NOT_ENOUGH_MONEY;
	}

	int const iSoulCount = PgSocketFormula::GetRemoveNeedSoul(kItem, iSocket_Order);

	size_t const kHasSoulCount = pkInv->GetTotalCount(ITEM_SOUL_NO);

	if(iSoulCount > static_cast<int>(kHasSoulCount))
	{
		return E_GS_NOT_ENOUGH_SOUL;
	}

	CONT_PLAYER_MODIFY_ORDER kOrder;

	SPMOD_Add_Any DelSoul(ITEM_SOUL_NO,-iSoulCount);
	SPMO MODS(IMET_ADD_ANY,pUser->GetID(),DelSoul);
	kOrder.push_back(MODS);

	{//소울 사용개수에 따른 업적
		PgAddAchievementValue kMA( AT_ACHIEVEMENT_SOULEATER, iSoulCount, m_kGndKey );
		kMA.DoAction( pUser, NULL );
	}

	SPMOD_Add_Money ModifyMoney(-i64Cost);
	SPMO MOM(IMET_ADD_MONEY,pUser->GetID(),ModifyMoney);
	kOrder.push_back(MOM);

	SPMOD_Modify_Count ModifyCount(kCard,kCardPos,-1);
	SPMO MOF(IMET_MODIFY_COUNT,pUser->GetID(),ModifyCount);
	kOrder.push_back(MOF);	

	switch( iSocket_Order )
	{
	case PgSocketFormula::SII_FIRST:
		{
			kEnchant.GenSocketState(GSS_GEN_SUCCESS);
			kEnchant.MonsterCard(0);
		}break;
	case PgSocketFormula::SII_SECOND:
		{
			kEnchant.GenSocketState2(GSS_GEN_SUCCESS);
			kEnchant.MonsterCard2(0);
		}break;
	case PgSocketFormula::SII_THIRD:
		{
			kEnchant.GenSocketState3(GSS_GEN_SUCCESS);
			kEnchant.MonsterCard3(0);		
		}break;
	default:
		{
		}break;
	}

	//kEnchant.GenSocketState(GSS_GEN_SUCCESS);
	//kEnchant.MonsterCard(0);
	kEnchant.IsMCTimeOuted(0);

	SPMOD_Enchant kEnchantData( kItem, kItemPos, kEnchant);//변경될 인첸트
	SPMO kIMO(IMET_MODIFY_ENCHANT,pUser->GetID(), kEnchantData);
	kOrder.push_back(kIMO);

	SMonsterCardTimeLimit kTimeLimit;
	if(true == kItem.Get(kTimeLimit))
	{
		kOrder.push_back(SPMO(IMET_MODIFY_EXTEND_DATA,pUser->GetID(),SPMOD_ExtendData(kItem,kItemPos,kTimeLimit,true))); // 기간제 몬스터 카드이면 기간제 정보도 삭제 한다.
	}

	BM::Stream kPacket(PT_M_C_ANS_REMOVE_MONSTERCARD);
	PgAction_ReqModifyItem kAction( CIE_Remove_MonsterCard, m_kGndKey, kOrder,kPacket );
	kAction.DoAction(pUser, NULL);
	return S_OK;
}

bool PgAction_ReqDestroySocket::DoAction(CUnit * pUser,CUnit * pTarget)
{
	HRESULT const kRet = Process(pUser);
	if(S_OK == kRet)
	{
		return true;
	}

	BM::Stream kPacket(PT_M_C_ANS_RESET_MONSTERCARD);
	kPacket.Push(kRet);
	pUser->Send(kPacket);
	return true;
}

HRESULT PgAction_ReqDestroySocket::Process(CUnit * pUser)
{
	SItemPos kCardPos;
	SItemPos kItemPos;

	m_kPacket.Pop(kCardPos);
	m_kPacket.Pop(kItemPos);

	int iSocket_Order = 0;
	m_kPacket.Pop(iSocket_Order);

	PgBase_Item kItem,kCard;

	PgInventory * pkInv = pUser->GetInven();

	if( (S_OK != pkInv->GetItem(kCardPos,kCard)) || 
		(S_OK != pkInv->GetItem(kItemPos,kItem)))
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	CItemDef const *pCardDef = kItemDefMgr.GetDef(kCard.ItemNo());
	if(!pCardDef)
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	if(UICT_DESTROYCARD != pCardDef->GetAbil(AT_USE_ITEM_CUSTOM_TYPE))
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	E_ITEM_GRADE const kItemGrade = GetItemGrade(kItem);

	SEnchantInfo kEnchant = kItem.EnchantInfo();

	int iDestroySocketIndex = GSS_GEN_NONE;
	int iDestroySocketCard = 0;

	switch( iSocket_Order )
	{
	case PgSocketFormula::SII_FIRST:
		{
			iDestroySocketIndex = kEnchant.GenSocketState();
			iDestroySocketCard = kEnchant.MonsterCard();
		}break;
	case PgSocketFormula::SII_SECOND:
		{
			iDestroySocketIndex = kEnchant.GenSocketState2();
			iDestroySocketCard = kEnchant.MonsterCard2();
		}break;
	case PgSocketFormula::SII_THIRD:
		{
			iDestroySocketIndex = kEnchant.GenSocketState3();
			iDestroySocketCard = kEnchant.MonsterCard3();
		}break;
	default:
		{
			return E_GS_INVALID_IDX;
		}break;
	}

	if(GSS_GEN_NONE == iDestroySocketIndex)
	{
		return E_GS_NOT_GEN;
	}

	if(GSS_GEN_SUCCESS == iDestroySocketIndex && 0 == iDestroySocketCard)
	{
		return E_GS_NOT_SET_CARD;
	}

	__int64 const i64Cost = PgSocketFormula::GetRemoveNeedCost(kItem, iSocket_Order);

	if(i64Cost > pUser->GetAbil64(AT_MONEY))
	{
		return E_GS_NOT_ENOUGH_MONEY;
	}

	int const iSoulCount = PgSocketFormula::GetRemoveNeedSoul(kItem, iSocket_Order);

	size_t const kHasSoulCount = pkInv->GetTotalCount(ITEM_SOUL_NO);

	if(iSoulCount > static_cast<int>(kHasSoulCount))
	{
		return E_GS_NOT_ENOUGH_SOUL;
	}

	CONT_PLAYER_MODIFY_ORDER kOrder;

	SPMOD_Add_Any DelSoul(ITEM_SOUL_NO,-iSoulCount);
	SPMO MODS(IMET_ADD_ANY,pUser->GetID(),DelSoul);
	kOrder.push_back(MODS);

	{//소울 사용개수에 따른 업적
		PgAddAchievementValue kMA( AT_ACHIEVEMENT_SOULEATER, iSoulCount, m_kGndKey );
		kMA.DoAction( pUser, NULL );
	}

	SPMOD_Add_Money ModifyMoney(-i64Cost);
	SPMO MOM(IMET_ADD_MONEY,pUser->GetID(),ModifyMoney);
	kOrder.push_back(MOM);

	SPMOD_Modify_Count ModifyCount(kCard,kCardPos,-1);
	SPMO MOF(IMET_MODIFY_COUNT,pUser->GetID(),ModifyCount);
	kOrder.push_back(MOF);

	switch( iSocket_Order )
	{
	case PgSocketFormula::SII_FIRST:
		{
			kEnchant.GenSocketState(GSS_GEN_NONE);
			kEnchant.MonsterCard(0);
		}break;
	case PgSocketFormula::SII_SECOND:
		{
			kEnchant.GenSocketState2(GSS_GEN_NONE);
			kEnchant.MonsterCard2(0);
		}break;
	case PgSocketFormula::SII_THIRD:
		{
			kEnchant.GenSocketState3(GSS_GEN_NONE);
			kEnchant.MonsterCard3(0);		
		}break;
	default:
		{
		}break;
	}

	//kEnchant.GenSocketState(GSS_GEN_SUCCESS);
	//kEnchant.MonsterCard(0);
	kEnchant.IsMCTimeOuted(0);

	SPMOD_Enchant kEnchantData( kItem, kItemPos, kEnchant);//변경될 인첸트
	SPMO kIMO(IMET_MODIFY_ENCHANT,pUser->GetID(), kEnchantData);
	kOrder.push_back(kIMO);

	SMonsterCardTimeLimit kTimeLimit;
	if(true == kItem.Get(kTimeLimit))
	{
		kOrder.push_back(SPMO(IMET_MODIFY_EXTEND_DATA,pUser->GetID(),SPMOD_ExtendData(kItem,kItemPos,kTimeLimit,true))); // 기간제 몬스터 카드이면 기간제 정보도 삭제 한다.
	}

	BM::Stream kPacket(PT_M_C_ANS_RESET_MONSTERCARD);
	PgAction_ReqModifyItem kAction( CIE_Del_MonsterCard, m_kGndKey, kOrder,kPacket );
	kAction.DoAction(pUser, NULL);
	return S_OK;
}


bool PgAction_ReqExtractionSocket::DoAction(CUnit * pUser,CUnit * pTarget)
{
	HRESULT const kRet = Process(pUser);
	if(S_OK == kRet)
	{
		return true;
	}

	BM::Stream kPacket(PT_M_C_ANS_EXTRACTION_MONSTERCARD);
	kPacket.Push(kRet);
	pUser->Send(kPacket);
	return true;
}

HRESULT PgAction_ReqExtractionSocket::Process(CUnit * pUser)
{
	SItemPos kItemPos;
	int iSocket_Order = 0;
	int iUseCashItemCount = 0;
	int iNeedItemExtractionCounttemp = 0;
	
	m_kPacket.Pop(kItemPos);
	m_kPacket.Pop(iSocket_Order);
	m_kPacket.Pop(iUseCashItemCount);

	PgBase_Item kItem;

	PgInventory * pkInv = pUser->GetInven();

	if( S_OK != pkInv->GetItem(kItemPos,kItem) )
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	E_ITEM_GRADE const kItemGrade = GetItemGrade(kItem);

	SEnchantInfo kEnchant = kItem.EnchantInfo();

	int iExtractionSocketIndex = GSS_GEN_NONE;
	int iExtractionSocketCard = 0;

	switch( iSocket_Order )
	{
	case PgSocketFormula::SII_FIRST:
		{
			iExtractionSocketIndex = kEnchant.GenSocketState();
			iExtractionSocketCard = kEnchant.MonsterCard();

			kEnchant.GenSocketState(GSS_GEN_FAIL);
			kEnchant.MonsterCard(0);
		}break;
	case PgSocketFormula::SII_SECOND:
		{
			iExtractionSocketIndex = kEnchant.GenSocketState2();
			iExtractionSocketCard = kEnchant.MonsterCard2();

			kEnchant.GenSocketState2(GSS_GEN_FAIL);
			kEnchant.MonsterCard2(0);
		}break;
	case PgSocketFormula::SII_THIRD:
		{
			iExtractionSocketIndex = kEnchant.GenSocketState3();
			iExtractionSocketCard = kEnchant.MonsterCard3();

			kEnchant.GenSocketState3(GSS_GEN_FAIL);
			kEnchant.MonsterCard3(0);
		}break;
	default:
		{
			return E_GS_INVALID_IDX;
		}break;
	}

	if(GSS_GEN_FAIL == iExtractionSocketIndex)
	{
		return E_GS_FAIL_SOCKET;
	}

	if(GSS_GEN_SUCCESS != iExtractionSocketIndex ||  0 == iExtractionSocketCard)
	{
		return E_GS_NOT_GEN;
	}

	int iCardItemNo = 0;
	CONT_MONSTERCARD const *kCont = NULL;
	g_kTblDataMgr.GetContDef(kCont);
	if( kCont )
	{
		CONT_MONSTERCARD::key_type kKey(iSocket_Order, iExtractionSocketCard);
		CONT_MONSTERCARD::const_iterator iter = kCont->find(kKey);
		if( kCont->end() != iter )
		{
			iCardItemNo =  iter->second;
		}
	}
	CItemDef const *pMonsterCardItemDef = kItemDefMgr.GetDef(iCardItemNo);

	//1. 캐시아이템 수량
	int const iCashItemNo = pMonsterCardItemDef->GetAbil(AT_SOCET_CARD_EXTRACTION_CASH_ITEM);
	int const iInGameItemNo = pMonsterCardItemDef->GetAbil(AT_SOCET_CARD_EXTRACTION_ITEM_NAME);
	int iInGameItemCount = 0;

	PgSocketFormula::GetExtractionItemCount(pUser, pMonsterCardItemDef, iUseCashItemCount, iInGameItemCount);
	
	size_t const kHaveInGameItemCount = pkInv->GetTotalCount(iInGameItemNo);
	if(iInGameItemCount > static_cast<int>(kHaveInGameItemCount))
	{
		return E_GS_NOT_ENOUGH_ITEM;
	}

	if(iUseCashItemCount > static_cast<int>(pkInv->GetTotalCount(iCashItemNo)))
	{
		return E_GS_NOT_ENOUGH_ITEM;
	}

	PgBase_Item kCardItem;
	if(S_OK != ::CreateSItem(iCardItemNo, 1, GIOT_NONE, kCardItem))
	{
		return E_GS_NOT_FOUND_ITEM;
	}

	CONT_PLAYER_MODIFY_ORDER kOrder;

	SPMOD_Add_Any DelInGameItem(iInGameItemNo, -iInGameItemCount);
	kOrder.push_back( SPMO(IMET_ADD_ANY,pUser->GetID(),DelInGameItem) );

	SPMOD_Add_Any DelCashItem(iCashItemNo, -iUseCashItemCount);
	kOrder.push_back( SPMO(IMET_ADD_ANY,pUser->GetID(),DelCashItem) );
	
	kOrder.push_back( SPMO(IMET_INSERT_FIXED, pUser->GetID(), SPMOD_Insert_Fixed(kCardItem, SItemPos(), true)) );

	SPMOD_Enchant kEnchantData( kItem, kItemPos, kEnchant);//변경될 인첸트
	kOrder.push_back( SPMO(IMET_MODIFY_ENCHANT, pUser->GetID(), kEnchantData) );

	BM::Stream kPacket(PT_M_C_ANS_EXTRACTION_MONSTERCARD);
	kPacket.Push(iCardItemNo);
	PgAction_ReqModifyItem kAction( CIE_EXTRACTION_MonsterCard, m_kGndKey, kOrder, kPacket );
	return kAction.DoAction(pUser, NULL) ? S_OK : E_FAIL;
}


HRESULT PgAction_BasicOptionAmp::OnProcess(CUnit * pkCaster,CUnit * pkTarget)
{
	SItemPos	kItemPos,
				kInsuranceItemPos;
	m_kPacket.Pop(kItemPos);
	m_kPacket.Pop(kInsuranceItemPos);

	CONT_PLAYER_MODIFY_ORDER kOrder;
	PgInventory *pInv = pkCaster->GetInven();

	PgPlayer * pkPlayer = dynamic_cast<PgPlayer *>(pkCaster);
	if(NULL == pkPlayer)
	{
		return E_BASICOPTIONAMP_NOT_FOUND_ITEM;
	}

	PgBase_Item kItem;
	if(S_OK != pInv->GetItem(kItemPos, kItem))
	{
		return E_BASICOPTIONAMP_NOT_FOUND_ITEM;
	}
	
	if( LOCAL_MGR::NC_JAPAN == g_kLocal.ServiceRegion() 
		&& CheckIsCashItem(kItem))
	{//일본의 경우 캐시 아이템은 소울 크래프트 관련 작업 불가
		return E_BASICOPTIONAMP_CANT_AMP_ITEM;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if(!pItemDef)
	{
		return E_BASICOPTIONAMP_NOT_FOUND_ITEM;
	}

	int const iLevelLimit = pItemDef->GetAbil(AT_LEVELLIMIT);
	int const iBasicAmpLv = kItem.EnchantInfo().BasicAmpLv();
	SDefBasicOptionAmp const * pkOptionAmpInfo = PgItemRarityUpgradeFormula::GetBasicOptionAmp(GetEquipType(pItemDef), iLevelLimit, iBasicAmpLv+1);
	if( !pkOptionAmpInfo )
	{
		return E_BASICOPTIONAMP_CANT_AMP_ITEM;
	}

	if(!pItemDef->CanEquip() 
		|| (ICMET_Cant_SoulCraft & pItemDef->GetAbil(AT_ATTRIBUTE))
		|| kItem.EnchantInfo().IsBinding()
		|| kItem.EnchantInfo().IsCurse()
		|| kItem.EnchantInfo().IsSeal())
	{
		return E_BASICOPTIONAMP_CANT_AMP_ITEM;
	}

	E_ITEM_GRADE const kItemGrade = GetItemGrade(kItem);

	m_bUseInsurance = false;

	if(kInsuranceItemPos != SItemPos::NullData())	// 성공 확률 보정 아이템 사용 체크
	{
		PgBase_Item kInsuranceItem;
		if(S_OK != pInv->GetItem(kInsuranceItemPos, kInsuranceItem))
		{
			return E_BASICOPTIONAMP_NOT_FOUND_INSURANCEITEM;
		}

		if(pkOptionAmpInfo->iInsuranceItemNo == kInsuranceItem.ItemNo())
		{
			kOrder.push_back(SPMO(IMET_MODIFY_COUNT,pkCaster->GetID(), SPMOD_Modify_Count(kInsuranceItem,kInsuranceItemPos,-1)));
			m_bUseInsurance = true;
		}
	}

	__int64 const i64NeedCost = pkOptionAmpInfo->iNeedCost;
	int const iNeedSoul = pkOptionAmpInfo->iNeedSoulCount;
	int const iSuccessRate = pkOptionAmpInfo->iSuccessRate;

	__int64 const i64CurMoney = pInv->Money();
	if(i64CurMoney < i64NeedCost)
	{
		return E_BASICOPTIONAMP_NOT_ENOUGH_MONEY;
	}

	if(pInv->GetTotalCount(ITEM_SOUL_NO) < iNeedSoul)
	{
		return E_BASICOPTIONAMP_NOT_ENOUGH_SOUL;
	}

	if(pInv->GetTotalCount(pkOptionAmpInfo->iAmpItemNo) < pkOptionAmpInfo->iAmpItemCount)
	{
		return E_BASICOPTIONAMP_NOT_ENOUGH_AMPITEM;
	}

	kOrder.push_back(SPMO(IMET_ADD_MONEY,pkCaster->GetID(), SPMOD_Add_Money(-i64NeedCost)));
	kOrder.push_back(SPMO(IMET_ADD_ANY,pkCaster->GetID(), SPMOD_Add_Any(pkOptionAmpInfo->iAmpItemNo,-pkOptionAmpInfo->iAmpItemCount)));
	kOrder.push_back(SPMO(IMET_ADD_ANY,pkCaster->GetID(), SPMOD_Add_Any(ITEM_SOUL_NO,-iNeedSoul)));

	{//소울 사용개수에 따른 업적
		PgAddAchievementValue kMA( AT_ACHIEVEMENT_SOULEATER, iNeedSoul, m_kGndKey );
		kMA.DoAction( pkCaster, NULL );
	}

	bool const bGodHand = (1 == pkCaster->GetAbil( AT_GM_GODHAND ));

	PgBase_Item kItemCopy = kItem;
	SEnchantInfo kNewEnchantInfo = kItemCopy.EnchantInfo();

	HRESULT hResult = E_BASICOPTIONAMP_SUCCESS;
	//10000을 넘게 셋팅, 그 아랫자리로 확률돌림.
	if( lwIsRandSuccess(iSuccessRate) || bGodHand )
	{
		kNewEnchantInfo.BasicAmpLv(kNewEnchantInfo.BasicAmpLv()+1);
	}
	else
	{
		if(!m_bUseInsurance)
		{
			kNewEnchantInfo.BasicAmpLv(0);
			hResult = E_BASICOPTIONAMP_FAIL_BROKEN;
		}
		else
		{
			hResult = E_BASICOPTIONAMP_FAIL;
		}
	}

	kItemCopy.EnchantInfo(kNewEnchantInfo);
	kOrder.push_back(SPMO(IMET_MODIFY_ENCHANT, pkCaster->GetID(), SPMOD_Enchant( kItem, kItemPos, kNewEnchantInfo)));

	BM::Stream kPacket(PT_M_C_ANS_BASIC_OPTION_AMP);
	kPacket.Push(hResult);
	kPacket.Push(m_bUseInsurance);// 보험 아이템 사용 여부 전송
	kItemCopy.WriteToPacket(kPacket);
	PgAction_ReqModifyItem kAction(CIE_BasicOptionAmp, m_kGndKey, kOrder,kPacket);
	kAction.DoAction(pkCaster, NULL);

	return hResult;
}

bool PgAction_BasicOptionAmp::DoAction(CUnit* pkCaster, CUnit* pkTarget)
{
	HRESULT const kResult = OnProcess(pkCaster,pkTarget);

	switch(kResult)
	{
	case E_BASICOPTIONAMP_FAIL:
	case E_BASICOPTIONAMP_FAIL_BROKEN:
	case E_BASICOPTIONAMP_SUCCESS:
		{
			return true;
		}break;
	}

	BM::Stream kPacket(PT_M_C_ANS_BASIC_OPTION_AMP);
	kPacket.Push(kResult);
	kPacket.Push(m_bUseInsurance);// 보험 아이템 사용 여부 전송
	pkCaster->Send(kPacket);
	return true;
}