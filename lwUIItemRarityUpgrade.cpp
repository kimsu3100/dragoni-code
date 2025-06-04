#include "StdAfx.h"
#include "lwUI.h"
#include "lwUIItemRarityUpgrade.h"
#include "lwUIToolTip.h"
#include "PgPilot.h"
#include "PgPilotMan.h"
#include "PgUIScene.h"
#include "Variant/PgPlayer.h"
#include "ServerLib.h"
#include "PgNetwork.h"
#include "PgNifMan.H"
#include "PgUIModel.h"

#include "variant/item.h"
#include "lohengrin/packettype.h"
#include "PgChatMgrClient.h"
#include "PgSoundMan.h"
#include "Variant/PgItemRarityUpgradeFormula.h"
#include "lwHomeUI_Script.h"
#include "PgMobileSuit.h"

float const RARITY_PROGRESS_TIME = 0.667f; //���� �ð��� 1��
float const UIMODEL_RARITY_EFFECT_RESULT_TIME = 0.53f;	//��� ����Ʈ ���� �ð�
extern int const SOUL_ITEM_NO = 79000030;
int const CRYSTAL_ITEM_NO_BASE = 20700000;
int const ISURANCE_ITEM_NO_BASE = 98000040;	//���� ������.
int const PROBABILITY_ITEM_NO_BASE = 79000210;

int const MAX_RARITY = 100;

namespace PgRarityUpgradeUIUtil
{
	std::wstring const kRarityUpgradeUIName(_T("SFRM_ITEM_RARITY_UPGRADE"));
	std::wstring const kRarityUpgradeSelectUIName(_T("ITEM_RARITY_UPGRADE_SELECT"));

	lwUIWnd CallRarityUpgradeUI(lwGUID kNpcGuid)
	{
		g_kItemRarityUpgradeMgr.NpcGuid( kNpcGuid() );
		CXUI_Wnd* pkTopWnd = XUIMgr.Activate( kRarityUpgradeUIName );
		if( pkTopWnd )
		{
			RegistUIAction(pkTopWnd);
			return lwUIWnd(pkTopWnd);
		}
		return lwUIWnd(NULL);
	}

	void CallRarityUpgradeSelectUI(lwGUID kNpcGuid)
	{
		CXUI_Wnd* pkTopWnd = XUIMgr.Activate( kRarityUpgradeSelectUIName );
		if( pkTopWnd )
		{
			BM::Stream kCustomData;
			kCustomData.Push( kNpcGuid() );
			pkTopWnd->SetCustomData( kCustomData.Data() );
		}
	}

	bool CheckRarityUseOKInsureItem(DWORD const iItemNo)
	{
		PgPlayer*	pkPlayer = g_kPilotMan.GetPlayerUnit();
		if( !pkPlayer )
		{
			return false;
		}

		PgInventory*	pkInv = pkPlayer->GetInven();
		if( !pkInv )
		{
			return false;
		}

		GET_DEF(CItemDefMgr, kItemDefMgr);
		CItemDef const* pDef = kItemDefMgr.GetDef(iItemNo);
		if( pDef )
		{
			int const iAttribute = pDef->GetAbil(AT_ATTRIBUTE);
			if( (iAttribute & ICMET_Cant_SoulCraft) == ICMET_Cant_SoulCraft )
			{
				return false;
			}
		}
		return true;
	}
};

void ClearRarityUpgradeUI()
{
	XUI::CXUI_Wnd* pkWnd = XUIMgr.Get( PgRarityUpgradeUIUtil::kRarityUpgradeUIName );
	if (!pkWnd)
	{
		return;
	}

	for (int i = 0; i < 4; ++i)
	{
		BM::vstring kString(L"ICN_MATERIAL");
		kString+=i;
		XUI::CXUI_Wnd* pkMat = pkWnd->GetControl(kString);
		if (pkMat)
		{
			lwUIWnd(pkMat).SetCustomData<int>(0);
		}

		kString = L"BTN_REG";
		kString += i;
		XUI::CXUI_Wnd* pReg = pkWnd->GetControl(kString);
		if( pReg )
		{
			pReg->Visible(false);
		}

		kString = L"BTN_DEREG";
		kString += i;
		XUI::CXUI_Wnd* pDeReg = pkWnd->GetControl(kString);
		if( pReg )
		{
			pDeReg->Visible(false);
		}
	}

	XUI::CXUI_Wnd* pkSrc = pkWnd->GetControl(L"ICN_SRC");
	if (pkSrc)
	{
		lwUIWnd(pkSrc).SetCustomData<int>(0);
	}

	XUI::CXUI_Wnd* pkSoul = pkWnd->GetControl(L"ICN_MATERIAL_SOUL");
	if (pkSoul)
	{
		lwUIWnd(pkSoul).SetCustomData<int>(0);
	}

	XUI::CXUI_Wnd* pkSdw = pkWnd->GetControl(L"SFRM_SHADOW");
	if (pkSdw)
	{
		pkSdw->Text(L"");
	}

	pkWnd->SetInvalidate();
}

void InitMaterialBtnState()
{
	XUI::CXUI_Wnd* pkWnd = XUIMgr.Get( PgRarityUpgradeUIUtil::kRarityUpgradeUIName );
	if (!pkWnd)
	{
		return;
	}

	for(int i = PgItemRarityUpgradeMgr::RIT_SOUL; i <= PgItemRarityUpgradeMgr::RIT_INSUR_ITEM; ++i)
	{
		g_kItemRarityUpgradeMgr.SetSrcMaterialBtnInit(pkWnd, i, true);
	}
}

lwUIItemRarityUpgrade::lwUIItemRarityUpgrade(lwUIWnd kWnd)
{
	self = kWnd.GetSelf();
}

void lwUIItemRarityUpgrade::CallCheckInsureItem()
{
	PgPlayer* pkPlayer = g_kPilotMan.GetPlayerUnit();
	if( pkPlayer )
	{
		PgInventory* pkInv = pkPlayer->GetInven();
		if( pkInv )
		{
			if( g_kItemRarityUpgradeMgr.AttachElement() != E_PPTY_CURSE )
			{
				ContHaveItemNoCount	rkOut;
				if( S_OK != pkInv->GetItems(UICT_ENCHANT_INSURANCE, rkOut) )
				{
					CallCommonMsgYesNoBox(TTW(3301), 3302, 3303, lwPacket(NULL), true, MBT_RARITY_INSURE_OKCANCEL, NULL);
					return;
				}
			}
			CallComfirmMessageBox();
		}
	}
}


bool lwUIItemRarityUpgrade::RegisterWrapper(lua_State *pkState)
{
	using namespace lua_tinker;

	def(pkState, "CallRarityUpgradeUI", PgRarityUpgradeUIUtil::CallRarityUpgradeUI);
	def(pkState, "CallRarityUpgradeSelectUI", PgRarityUpgradeUIUtil::CallRarityUpgradeSelectUI);
	
	class_<lwUIItemRarityUpgrade>(pkState, "ItemRarityUpgradeWnd")
		.def(pkState, constructor<lwUIWnd>())
		.def(pkState, "DisplaySrcIcon", &lwUIItemRarityUpgrade::DisplaySrcIcon)
		.def(pkState, "DisplayNeedItemIcon", &lwUIItemRarityUpgrade::DisplayNeedItemIcon)
		.def(pkState, "DisplayResultItemIcon", &lwUIItemRarityUpgrade::DisplayResultItem)
		.def(pkState, "ClearUpgradeData", &lwUIItemRarityUpgrade::ClearUpgradeData)
		.def(pkState, "SendReqRarityUpgrade", &lwUIItemRarityUpgrade::SendReqRarityUpgrade)
		.def(pkState, "GetUpgradeNeedMoney", &lwUIItemRarityUpgrade::GetUpgradeNeedMoney)
		.def(pkState, "CallComfirmMessageBox", &lwUIItemRarityUpgrade::CallComfirmMessageBox)
		.def(pkState, "CallCheckInsureItem", &lwUIItemRarityUpgrade::CallCheckInsureItem)
		.def(pkState, "Clear", &lwUIItemRarityUpgrade::Clear)
		.def(pkState, "CheckOK", &lwUIItemRarityUpgrade::CheckOK)
		.def(pkState, "GetNowNeedItemCount", &lwUIItemRarityUpgrade::GetNowNeedItemCount)
		.def(pkState, "OnDisplay", &lwUIItemRarityUpgrade::OnDisplay)
		.def(pkState, "OnTick", &lwUIItemRarityUpgrade::OnTick)
		.def(pkState, "InProgress", &lwUIItemRarityUpgrade::InProgress)
		.def(pkState, "SetAttachElement", &lwUIItemRarityUpgrade::SetAttachElement)
		.def(pkState, "SetLockSlot", &lwUIItemRarityUpgrade::SetLockSlot)
		.def(pkState, "SetLockSlot", &lwUIItemRarityUpgrade::SetLockSlot)
		.def(pkState, "SetMaterialItem", &lwUIItemRarityUpgrade::SetMaterialItem)
	;

	def(pkState, "SetExplaneText", &lwUIItemRarityUpgrade::SetExplaneText);


	return true;
}

void lwUIItemRarityUpgrade::SetMaterialItem(int iType, bool bNoBuyMsg)
{
	g_kItemRarityUpgradeMgr.SetMaterialItem(self->Parent(), iType, bNoBuyMsg);
}

void lwUIItemRarityUpgrade::SetExplaneText()
{
	XUI::CXUI_Wnd* pMainUI = XUIMgr.Get(PgRarityUpgradeUIUtil::kRarityUpgradeUIName);
	if( pMainUI )
	{
		XUI::CXUI_Wnd* pText = pMainUI->GetControl(L"SFRM_SHADOW");
		if( pText )
		{
			pText->Text(g_kItemRarityUpgradeMgr.GetExplane());
		}
	}
}

void lwUIItemRarityUpgrade::SetLockSlot(int const iIndex)
{
	if (!self)
	{
		return;
	}

	if( iIndex == 3 || iIndex == 1 )
	{
		if(g_kItemRarityUpgradeMgr.AttachElement() == E_PPTY_CURSE)
		{
			if(!self->Visible())
			{
				self->Visible(true);
			}
		}
		else
		{
			self->Visible(false);
		}
	}
}

void lwUIItemRarityUpgrade::Clear(bool const bClearAll)
{
	g_kItemRarityUpgradeMgr.Clear(bClearAll);
}

void lwUIItemRarityUpgrade::DisplaySrcIcon()
{
	g_kItemRarityUpgradeMgr.DisplaySrcItem(self);
}

void lwUIItemRarityUpgrade::DisplayResultItem()
{
	g_kItemRarityUpgradeMgr.DisplayResultItem(self);
}

void lwUIItemRarityUpgrade::DisplayNeedItemIcon(int iIndex)
{
	if (!self)
	{
		return;
	}

	g_kItemRarityUpgradeMgr.DisplayNeedItemIcon( iIndex, self );
	assert(NULL && "lwUIItemRarityUpgrade::DisplayNeedItemIcon");
}

void lwUIItemRarityUpgrade::ClearUpgradeData()
{
	g_kItemRarityUpgradeMgr.RunProgressEffect(false);
	g_kItemRarityUpgradeMgr.Clear();
}

bool lwUIItemRarityUpgrade::SendReqRarityUpgrade(bool bIsTrueSend)
{
	if (!bIsTrueSend)
	{
		g_kItemRarityUpgradeMgr.RunProgressEffect();
		g_kItemRarityUpgradeMgr.InProgress(true);
		g_kItemRarityUpgradeMgr.StartTime(g_pkApp->GetAccumTime());
	}
	
	return g_kItemRarityUpgradeMgr.SendReqRarityUpgrade(bIsTrueSend);
}

int lwUIItemRarityUpgrade::GetUpgradeNeedMoney()
{
	return g_kItemRarityUpgradeMgr.GetUpgradeNeedMoney();
}

void lwUIItemRarityUpgrade::CallComfirmMessageBox( const bool bIsModal )
{
	g_kItemRarityUpgradeMgr.CallComfirmMessageBox( bIsModal );
}

void lwUIItemRarityUpgrade::SetAttachElement(int iType)
{
	g_kItemRarityUpgradeMgr.AttachElement((EPropertyType)iType);
}

void Recv_PT_C_M_ANS_ITEM_RARITY_UPGRADE(BM::Stream* pkPacket)
{
	SPT_M_C_ANS_ITEM_RARITY_UPGRADE kStruct;
	kStruct.ReadFromPacket(*pkPacket);
	
	if(IRUR_NONE!=kStruct.Result())
	{
		char szName[100] = "EnchantFail";
		std::wstring wstrWarnMessage;
		ENoticeLevel eLevel = EL_Warning;
		g_kItemRarityUpgradeMgr.RecentResult(kStruct.Result());
		//g_kItemRarityUpgradeMgr.RecentResult(PIUR_NOT_ENOUGH_RES);
		bool bCanInsurance = false;
		pkPacket->Pop(bCanInsurance);
		ContHaveItemNoCount	rkOut;

		if(bCanInsurance == true &&
		   g_kPilotMan.GetPlayerUnit() &&
		   g_kPilotMan.GetPlayerUnit()->GetInven())
		{
			g_kPilotMan.GetPlayerUnit()->GetInven()->GetItems(UICT_ENCHANT_INSURANCE, rkOut);
			ContHaveItemNoCount::const_iterator it = rkOut.find(g_kItemRarityUpgradeMgr.InsureItemNo());
			if (it == rkOut.end() || it->second <= 1)
				g_kItemRarityUpgradeMgr.Clear(false);
		}

		switch(kStruct.Result())
		{
		case IRUR_SUCCESS://	= 1,
			{
				eLevel = EL_Normal;
				sprintf(szName, "EnchantSuccess");
				if (E_PPTY_CURSE==g_kItemRarityUpgradeMgr.AttachElement())
				{
					wstrWarnMessage = ::TTW(1480);;//	���ָ� Ǯ�����ϴ�.
				}
				else
				{
					wstrWarnMessage = ::TTW(1401);;//	���ÿ� ���� �Ͽ����ϴ�
				}
			}break;
		case IRUR_NOT_ENOUGH_SOUL:
		case IRUR_NOT_ENOUGH_CRYSTALSTONE:
			{
				wstrWarnMessage = ::TTW(1402);;
			}break;
		case IRUR_INVALID_PROPERTY:	// ���� ���� ���� �Ӽ� �̴�.
			{
				wstrWarnMessage = ::TTW(1403);;
			}break;
		case IRUR_FAIL:				// ����
			{
				if (E_PPTY_CURSE==g_kItemRarityUpgradeMgr.AttachElement())
				{
					wstrWarnMessage = ::TTW(1481);;//	����Ǯ�� ����
				}
				else
				{
					if( bCanInsurance )
					{
						wstrWarnMessage = ::TTW(1424);;
					}
					else
					{
						wstrWarnMessage = ::TTW(1404);;
					}
				}
			}break;
		case IRUR_FAIL_AND_BROKEN:	// �����ؼ� ����
			{
				wstrWarnMessage = ::TTW(1405);;
			}break;
		case IRUR_NOT_ENOUGH_MONEY: //	
			{
				wstrWarnMessage = ::TTW(1407);;//���� ���ڶ��ϴ�.
			}break;
		case IRUR_NOT_FOUND_TARGET_ITEM: //	= 2,//��� ����
		case IRUR_NOT_ABLE_UPGRADE_ITEM:
			{
				wstrWarnMessage = ::TTW(59002);;
			}break; 
		case IRUR_IS_SEALDING:
			{
				wstrWarnMessage = ::TTW(1409);;
			}break; 
		default:
			{
				return;
			}break;
		}

		g_kItemRarityUpgradeMgr.StartTime(g_pkApp->GetAccumTime());	//��� ����Ʈ��
		g_kSoundMan.PlayAudioSourceByID(NiAudioSource::TYPE_3D, szName, 0.0f, 80, 100, g_kPilotMan.GetPlayerActor());
		g_kItemRarityUpgradeMgr.InProgress(false);

		Notice_Show(wstrWarnMessage, eLevel, false);
	}
}

void lwUIItemRarityUpgrade::ReSetUpgradeData()
{
	//���� ��ġ ����
	SItemPos	kItemPos = g_kItemRarityUpgradeMgr.GetSrcItemPos();
	SItemPos	kInsurePos = g_kItemRarityUpgradeMgr.GetInsureItemPos();
	SItemPos	kProbPos = g_kItemRarityUpgradeMgr.GetProbabilityItemPos();

	//Ŭ����
	ClearRarityUpgradeUI();
	g_kItemRarityUpgradeMgr.Clear();

	if(g_kItemRarityUpgradeMgr.AttachElement() == E_PPTY_CURSE)
	{
		return;
	}

	//���� ���
	SIconInfo kIconInfo;
	kIconInfo.iIconGroup = kItemPos.x;
	kIconInfo.iIconKey = kItemPos.y;
	g_kItemRarityUpgradeMgr.SetItem(KUIG_ITEM_RARITY_UPGRADE_SRC, kIconInfo);
	lwUIItemRarityUpgrade::SetExplaneText();

	//�μ����
	PgPlayer*	pkPlayer = g_kPilotMan.GetPlayerUnit();
	if( !pkPlayer )
	{
		return;
	}

	PgInventory*	pkInv = pkPlayer->GetInven();
	if( !pkInv )
	{
		return;
	}

	PgBase_Item kItem;
	ContHaveItemNoCount rkOut;
	if( kInsurePos != SItemPos::NullData() )
	{
		bool bSetInsure = false;

		if( S_OK == pkInv->GetItem( kInsurePos, kItem ) )
		{
			g_kItemRarityUpgradeMgr.SetMaterialItem(PgItemRarityUpgradeMgr::RIT_INSUR_ITEM, kInsurePos, true);
			bSetInsure = true;
		}

		if( !bSetInsure && S_OK == pkInv->GetItems(UICT_ENCHANT_INSURANCE, rkOut) )
		{
			if( g_kItemRarityUpgradeMgr.InsureItemNo() != 0 )
			{
				ContHaveItemNoCount::iterator iter = rkOut.find(g_kItemRarityUpgradeMgr.InsureItemNo());
				if( iter != rkOut.end() )
				{
					ContHaveItemNoCount::key_type const& kItemNo = iter->first;

					if( S_OK == pkInv->GetFirstItem(kItemNo, kInsurePos) )
					{
						g_kItemRarityUpgradeMgr.SetMaterialItem(PgItemRarityUpgradeMgr::RIT_INSUR_ITEM, kInsurePos, true);
					}
				}
				else
				{
					iter = rkOut.begin();
					while( iter != rkOut.end() )
					{
						ContHaveItemNoCount::key_type const& kItemNo = iter->first;

						if( PgRarityUpgradeUIUtil::CheckRarityUseOKInsureItem( kItemNo ) )
						{
							if( S_OK == pkInv->GetFirstItem(kItemNo, kInsurePos) )
							{
								g_kItemRarityUpgradeMgr.SetMaterialItem(PgItemRarityUpgradeMgr::RIT_INSUR_ITEM, kInsurePos, true);
								break;
							}
						}
						++iter;
					}
				}
			}
		}
	}

	rkOut.clear();

	if( kProbPos != SItemPos::NullData() )
	{
		GET_DEF(CItemDefMgr, kItemDefMgr);
		if( S_OK == pkInv->GetItems(UICT_RARITY_SUCCESS, rkOut) )
		{
			if( S_OK == pkInv->GetItem(kItemPos, kItem) )
			{
				E_ITEM_GRADE eGrade = GetItemGrade(kItem);

				ContHaveItemNoCount::iterator iter = rkOut.begin();
				while( iter != rkOut.end() )
				{
					ContHaveItemNoCount::key_type const& kItemNo = iter->first;

					CItemDef const *pDef = kItemDefMgr.GetDef(kItemNo);
					if(pDef)
					{
						if( eGrade == pDef->GetAbil(AT_GRADE) )
						{
							if( S_OK == pkInv->GetFirstItem( kItemNo, kProbPos ) )
							{
								g_kItemRarityUpgradeMgr.SetMaterialItem(PgItemRarityUpgradeMgr::RIT_PROBABILITY, kProbPos, true);
								break;
							}
						}
					}
					++iter;
				}
			}
		}
	}
};

int lwUIItemRarityUpgrade::CheckOK()
{
	if (g_kItemRarityUpgradeMgr.IsChangedGuid())
	{
		return 1422;
	}
	__int64 const iNeedMoney = g_kItemRarityUpgradeMgr.GetUpgradeNeedMoney();
	CUnit *pkUnit = g_kPilotMan.GetPlayerUnit();
	if(pkUnit)
	{
		__int64 const iMoney = pkUnit->GetAbil64(AT_MONEY);
		if (iNeedMoney > iMoney)
		{
			return 1407;
		}
	}
	if (g_kItemRarityUpgradeMgr.CheckNeedItem())
	{
		return 1402;
	}
	return 0;
}

int const lwUIItemRarityUpgrade::GetNowNeedItemCount(int const iNeed) const
{
	return g_kItemRarityUpgradeMgr.GetNowNeedItemCount(iNeed);
}

void lwUIItemRarityUpgrade::OnDisplay()
{
	g_kItemRarityUpgradeMgr.OnDisplay();
}

void lwUIItemRarityUpgrade::OnTick(lwUIWnd kWnd)
{
	if (g_kItemRarityUpgradeMgr.InProgress() && RARITY_PROGRESS_TIME < g_pkApp->GetAccumTime() - g_kItemRarityUpgradeMgr.StartTime())
	{
		g_kItemRarityUpgradeMgr.InProgress(false);	//�ð� �� �����ϱ�
		g_kItemRarityUpgradeMgr.SendReqRarityUpgrade(true);
	}
	else if (!kWnd.IsNil())
	{
		g_kItemRarityUpgradeMgr.OnTick(kWnd.GetTotalLocation()());
	}
}

bool lwUIItemRarityUpgrade::InProgress()
{
	return g_kItemRarityUpgradeMgr.InProgress();
}

lwWString lwUIItemRarityUpgrade::GetExplane()
{
	return lwWString((std::wstring const &)g_kItemRarityUpgradeMgr.GetExplane());
}

void lwUIItemRarityUpgrade::ResultProcess()
{
	g_kItemRarityUpgradeMgr.ResultProcess();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//	PgItemRarityUpgradeMgr
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
char const UIMODEL_RARITY_EFFECT_NAME[] = "ef_UImix_01";
char const UIMODEL_RARITY_EFFECT_NIF_NAME[] = "ef_UImix_01_NIF";
char const UIMODEL_RARITY_EFFECT_PATH[] = "../Data/5_Effect/4_UI/ef_UImix_01_spin.nif";
char const UIMODEL_RARITY_EFFECT_NIF_SUCC_NAME[] = "ef_UImix_01_SUCC_NIF";
char const UIMODEL_RARITY_EFFECT_SUCC_PATH[] = "../Data/5_Effect/4_UI/ef_UImix_02_succ.nif";
char const UIMODEL_RARITY_EFFECT_NIF_FAIL_NAME[] = "ef_UImix_01_FAIL_NIF";
char const UIMODEL_RARITY_EFFECT_FAIL_PATH[] = "../Data/5_Effect/4_UI/ef_UImix_03_fail.nif";

const POINT2 UIMODEL_RARITY_POS(0, 0);

HRESULT PgItemRarityUpgradeMgr::CheckRarityBundle(PgBase_Item const& kItem, EPropertyType const eAttachElement)
{
	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(kItem.ItemNo());
	if( !pItemDef )
	{
		return RCE_FALSE;
	}

	if( !pItemDef->CanEquip() )
	{
		return RCE_FALSE;
	}

	switch( eAttachElement )
	{
	case E_PPTY_CURSE:
		{
			// ���� ������ AT_ATTRIBUTE ��� ������ ����
		}break;
	default:
		{
			if( (ICMET_Cant_SoulCraft & pItemDef->GetAbil(AT_ATTRIBUTE)) == ICMET_Cant_SoulCraft )
			{
				return RCE_FALSE;
			}
		}break;
	}

	E_ITEM_GRADE const eItemGrade = GetItemGrade(kItem);
	
	switch(eItemGrade)
	{
	case IG_SEAL:
		{
			return RCE_SEAL;
		}break;
	case IG_CURSE:
		{
			return RCE_CURSE;
		}break;
#if 0
	case IG_GOD:
		{
			return REC_GOD;
		}break;
#endif
	}
	
	if( LOCAL_MGR::NC_JAPAN == g_kLocal.ServiceRegion() 
		&& CheckIsCashItem(kItem))
	{//�Ϻ��� ��� ĳ�� �������� �ҿ� ũ����Ʈ ���� �۾� �Ұ�
		return RCE_FALSE;
	}

	return RCE_OK;
}

PgItemRarityUpgradeMgr::PgItemRarityUpgradeMgr()
{
	InitUIModel();
	Clear();
	StartTime(0);
	RecentResult(IRUR_NONE);
	AttachElement(E_PPTY_NONE);
	MyElement(E_PPTY_NONE);
	m_kPastResultItem.Clear();
}

void PgItemRarityUpgradeMgr::InitUIModel()
{
	m_pkWndUIModel = NULL;
	m_pkWndUIModel_Result = NULL;

	g_kUIScene.InitRenderModel(UIMODEL_RARITY_EFFECT_NAME, POINT2(230,230), UIMODEL_RARITY_POS, false);
	m_pkWndUIModel = g_kUIScene.FindUIModel(UIMODEL_RARITY_EFFECT_NAME);
	if (m_pkWndUIModel)
	{
		m_pkWndUIModel->AddNIF(UIMODEL_RARITY_EFFECT_NIF_NAME, g_kNifMan.GetNif(UIMODEL_RARITY_EFFECT_PATH), false, true);
		m_pkWndUIModel->AddNIF(UIMODEL_RARITY_EFFECT_NIF_SUCC_NAME, g_kNifMan.GetNif(UIMODEL_RARITY_EFFECT_SUCC_PATH), false, true);
		m_pkWndUIModel->AddNIF(UIMODEL_RARITY_EFFECT_NIF_FAIL_NAME, g_kNifMan.GetNif(UIMODEL_RARITY_EFFECT_FAIL_PATH), false, true);
		m_pkWndUIModel->SetCameraZoomMinMax(-300, 300);
		m_pkWndUIModel->CameraZoom(210.0f);
	}
}

void PgItemRarityUpgradeMgr::RunProgressEffect(bool bOn)
{
	if (m_pkWndUIModel)
	{
		m_pkWndUIModel->SetNIFEnableUpdate(UIMODEL_RARITY_EFFECT_NIF_NAME,bOn);
		if (bOn)
		{
			m_pkWndUIModel->SetEnableUpdate(bOn);
			m_pkWndUIModel->ResetNIFAnimation(UIMODEL_RARITY_EFFECT_NIF_NAME);
			m_pkWndUIModel->RenderFrame(NiRenderer::GetRenderer(), UIMODEL_RARITY_POS);
			m_pkWndUIModel->SetNIFEnableUpdate(UIMODEL_RARITY_EFFECT_NIF_SUCC_NAME,!bOn);
			m_pkWndUIModel->SetNIFEnableUpdate(UIMODEL_RARITY_EFFECT_NIF_FAIL_NAME,!bOn);
		}
	}
}

void PgItemRarityUpgradeMgr::RecentResult(EItemRarityUpgradeResult const & eResult)
{
	bool bSucc = false;
	bool bFail = false;
	m_RecentResult = eResult;
	if (IRUR_SUCCESS==eResult)
	{
		bSucc = true;
		m_pkWndUIModel->SetNIFEnableUpdate(UIMODEL_RARITY_EFFECT_NIF_SUCC_NAME, bSucc);
		m_pkWndUIModel->ResetNIFAnimation(UIMODEL_RARITY_EFFECT_NIF_SUCC_NAME);
	}
	else if (IRUR_FAIL==eResult || IRUR_FAIL_AND_BROKEN==eResult)
	{
		bFail = true;
		m_pkWndUIModel->SetNIFEnableUpdate(UIMODEL_RARITY_EFFECT_NIF_FAIL_NAME, bFail);
		m_pkWndUIModel->ResetNIFAnimation(UIMODEL_RARITY_EFFECT_NIF_FAIL_NAME);
	}
	
	m_pkWndUIModel->RenderFrame(NiRenderer::GetRenderer(), UIMODEL_RARITY_POS);
}

void PgItemRarityUpgradeMgr::OnDisplay()
{
	if (InProgress() || IRUR_NONE!=RecentResult())
	{
		if(m_pkWndUIModel)
		{
			g_kUIScene.AddToDrawListRenderModel(UIMODEL_RARITY_EFFECT_NAME);
		}
	}
}

void PgItemRarityUpgradeMgr::OnTick(POINT2 kPt)
{
	if (InProgress())
	{
		if(m_pkWndUIModel)
		{
			m_pkWndUIModel->RenderFrame(NiRenderer::GetRenderer(), kPt);
		}
	}
	else
	{
		if (UIMODEL_RARITY_EFFECT_RESULT_TIME < g_pkApp->GetAccumTime() - g_kItemRarityUpgradeMgr.StartTime())
		{
			RecentResult(IRUR_NONE);
		}
		else if (IRUR_NONE!=RecentResult())
		{
			if(m_pkWndUIModel)
			{
				m_pkWndUIModel->RenderFrame(NiRenderer::GetRenderer(), kPt);
			}
		}
	}
}

void PgItemRarityUpgradeMgr::Clear(bool const bAllClear)
{
	m_kResultItem.Clear();
	m_guidSrcItem = BM::GUID();
	m_kSrcItemPos.Clear();
	m_kInsureItemPos.Clear();
	m_kProbabilityItemPos.Clear();
	m_kItem.Clear();

	for (int i = 0; i < RIT_INSUR_ITEM+1; ++i)
	{
		m_kItemArray[i].Init();
	}

	InProgress(false);
	ClearRarityUpgradeUI();
	m_kExplane = L"";
	if( bAllClear )
	{
		m_kNpcGuid.Clear();
	}

	g_kSoundMan.StopAudioSourceByID("Enchant");
}

int PgItemRarityUpgradeMgr::CallComfirmMessageBox( const bool bIsModal )
{
	if(!m_kItem.IsEmpty())
	{
		bool bInsure = !(SItemPos::NullData() == m_kInsureItemPos);
		E_ITEM_GRADE const eItemGrade = GetItemGrade(m_kItem);
//		int iTextNo = 1418;
		int iTextNo = 1425;
		switch(eItemGrade)
		{
		case IG_NORMAL:
			iTextNo = 1425;
			break;
		case IG_RARE:
		case IG_UNIQUE:
		case IG_ARTIFACT:
		case IG_LEGEND:
		case IG_EPIC:
			{
				if(bInsure)
				{
					iTextNo = 1427;
				}
				else
				{
					iTextNo = 1426;
				}
			}
			break;

		case IG_SEAL:
			{
				goto __ERROR;
			}break;
		case IG_CURSE:
			{
				if (E_PPTY_CURSE!=AttachElement())
				{
					goto __ERROR;
				}
				iTextNo = 1419;
			}
			
		default:
			{//���׷��̵� �� �� ���� ���.
				
			}break;
		}
		
		XUI::CXUI_Wnd *pWnd =  XUIMgr.Call(_T("SFRM_MSG_RARITY_REFINE"), bIsModal );
		if(pWnd)
		{
			XUI::CXUI_Wnd *pColorWnd =  pWnd->GetControl(_T("SFRM_COLOR"));
			if(pColorWnd)
			{
				XUI::CXUI_Wnd *pSdwWnd =  pColorWnd->GetControl(_T("SFR_SDW"));
				if(pSdwWnd)
				{//Ŀ���� ������ �Ѱ���.
					//pSdwWnd->Text(TTW(iTextNo));


					//���� ����� �ؽ�Ʈ���� �°� �ø���.
					BM::vstring strMsg(L"");
					strMsg = TTW(iTextNo);
					pSdwWnd->Text(strMsg);

					CXUI_Style_String kStyle;
					const POINT2 ptTextSize = pSdwWnd->AdjustText(pSdwWnd->Font(), strMsg, kStyle, pSdwWnd->Width() );
					int iStretchOffset = ptTextSize.y - (pSdwWnd->Size().y - (pSdwWnd->TextPos().y * 2) );

					if(iStretchOffset > 0)
					{
						pSdwWnd->Size(POINT2(pSdwWnd->Size().x, pSdwWnd->Size().y + iStretchOffset));
						pColorWnd->Size(POINT2(pColorWnd->Size().x, pColorWnd->Size().y + iStretchOffset));
						pWnd->Size(POINT2(pWnd->Size().x, pWnd->Size().y + iStretchOffset));

						XUI::CXUI_Wnd* pBtnOK = pWnd->GetControl(_T("BTN_TRY_REFINE"));
						XUI::CXUI_Wnd* pBtnCancel = pWnd->GetControl(_T("BTN_CANCLE"));
						if(pBtnOK && pBtnCancel)
						{
							pBtnOK->Location(POINT2(pBtnOK->Location().x, pBtnOK->Location().y + iStretchOffset) );
							pBtnCancel->Location(POINT2(pBtnCancel->Location().x, pBtnCancel->Location().y + iStretchOffset) );
						}
					}

					return 0;
				}
			}
		}

		assert(pWnd);
		return 0;
	}
__ERROR:
	{
		if (!m_kItem.IsEmpty())
		{
			m_kItem.Clear();
		}
		lwAddWarnDataTT(59002);
		return 0;
	}

	return 0;
}

__int64 PgItemRarityUpgradeMgr::GetUpgradeNeedMoney()
{
	if(!m_kItem.IsEmpty())
	{
		E_ITEM_GRADE const eItemGrade = GetItemGrade(m_kItem);
		//return PgItemRarityUpgradeFormula::GetNeedEnchantCost(eItemGrade, m_kItem);

		__int64 i64Money = PgItemRarityUpgradeFormula::GetNeedEnchantCost(eItemGrade, m_kItem);
		if(g_pkWorld && g_pkWorld->IsHaveAttr(GATTR_MYHOME))// ����Ȩ ���ο� ������
		{
			i64Money = static_cast<int>(static_cast<float>(i64Money) * ((100.0f - lwHomeUI::GetMyHomeSideJobDiscountRate(MSJ_SOULCRAFT, MSJRT_GOLD)) / 100.0f ));
		}
		return i64Money;
	}
	return 0i64;
}

bool PgItemRarityUpgradeMgr::SetSrcItem(const SItemPos &rkItemPos)
{
	if(InProgress())
	{
		lwAddWarnDataTT(1408);
		return false;
	}
	if (rkItemPos.x && rkItemPos.y)
	{
		ClearRarityUpgradeUI();
		Clear();//Ŭ���� ��ƾ���.
	}

	PgPlayer* pkPlayer = g_kPilotMan.GetPlayerUnit();
	if(!pkPlayer){return false;}

	PgInventory *pInv = pkPlayer->GetInven();
	if(!pInv){return false;}

	int iErrorNo = 59002;//! ũ����Ʈ �� �� ���� ������.
	if (E_PPTY_CURSE==AttachElement())
	{
		iErrorNo = 59005;
	}
	MyElement((EPropertyType)pkPlayer->GetAbil(AT_FIVE_ELEMENT_TYPE_AT_BODY));	//�� �Ӽ�
	switch(rkItemPos.x)
	{
	case KUIG_EQUIP:
	case KUIG_CASH:
		{
			if(S_OK != pInv->GetItem(rkItemPos, m_kItem))
			{
				return false;
			}

			SEnchantInfo const& kEhtInfo = m_kItem.EnchantInfo();
			if(m_kItem.ItemNo() == SOUL_ITEM_NO 
			   || kEhtInfo.IsBinding()
			   )
			{
				goto __ERROR;
			}
			if(m_kItem.IsEmpty())
			{
				goto __ERROR;
			}

			
			E_ITEM_GRADE const eitemGrade = GetItemGrade(m_kItem);
			if(IG_EPIC <= eitemGrade)
			{
				iErrorNo = 59004;
				goto __ERROR;
			}

			switch( CheckRarityBundle(m_kItem, m_kAttachElement) )
			{
			case RCE_SEAL:
				{
					iErrorNo = 1409;
					goto __ERROR;
				}break;
			case RCE_CURSE:
				{
					if (E_PPTY_CURSE!=AttachElement())
					{
						iErrorNo = 1497;
						goto __ERROR;
					}
				}//break;
			case RCE_OK:
				{
					GET_DEF(CItemDefMgr, kItemDefMgr);
					CItemDef const *pItemDef = kItemDefMgr.GetDef(m_kItem.ItemNo());
					if( !pItemDef )
					{
						goto __ERROR;
					}

					E_ITEM_GRADE const eItemGrade = GetItemGrade(m_kItem);

					if (eItemGrade!=IG_CURSE && E_PPTY_CURSE==AttachElement())
					{
						iErrorNo = 59005;
						goto __ERROR;
					}
					if (!IsOnlyUseSoul())
					{
						int iType = pItemDef->GetAbil(AT_EQUIP_LIMIT);
						if (EQUIP_LIMIT_WEAPON!=iType && EQUIP_LIMIT_SHIRTS!=iType)
						{
							goto __ERROR;
						}
						if (PROPERTY_LEVEL_LIMIT <= m_kItem.EnchantInfo().AttrLv())
						{
							iErrorNo = 59006;
							goto __ERROR;
						}
					}

					m_guidSrcItem = m_kItem.Guid();//GUID ���
					m_kResultItem = m_kItem;
					if(m_kPastResultItem.Guid() != m_kResultItem.Guid())
					{
						m_kPastResultItem = m_kResultItem;
					}

					m_kSrcItemPos = rkItemPos;
					
					for (int i = 0; i<4; ++i)
					{
						if (!SetElementInfo(i, m_kItem, pInv, m_kItemArray))
						{
							iErrorNo = 59002;
							if (E_PPTY_CURSE==AttachElement())
							{
								iErrorNo = 59005;
							}
							goto __ERROR;
						}
					}
				}break;
#if 0
			case RCE_GOD:
				{
					iErrorNo = 59004;
					goto __ERROR;
				}break;
#endif
			default:
				{
					goto __ERROR;
				}break;
			}
		}break;
	case KUIG_FIT:
		{
			lwAddWarnDataTT(1406);
			return false;
		}break;
	case KUIG_CONSUME:
	case KUIG_ETC:
		{
			iErrorNo = 59007;
			if (E_PPTY_CURSE==AttachElement())
			{
				iErrorNo = 59005;
			}
			goto __ERROR;
		}break;
	default:
		{
			
		}break;
	}

	MakeExplane(m_kItemArray);

	return true;

__ERROR:
	Clear();
	lwAddWarnDataTT(iErrorNo);
	return false;
}


void PgItemRarityUpgradeMgr::DisplayNeedItemIcon(int const iNeedIndex, XUI::CXUI_Wnd *pWnd)
{
	if (!pWnd) { return; }
	int const iNull = 0;
	BM::vstring kString;

	if(m_kItem.IsEmpty()){goto __HIDE;}
	if(IsChangedGuid()){goto __HIDE;}//guid �ٲ������ ã�ư��簡.

	int iItemNo = 0;
	DWORD	dwMaterialItemNo = 0;
	bool bGray = false;	//������� �׸���

	switch(iNeedIndex)
	{
	case RIT_SOUL:
		{
			if (IsOnlyUseSoul() && RIT_SOUL!=iNeedIndex)	//���¸� �ø��°Ÿ�
			{
				goto __HIDE;
			}
			bGray = !(m_kItemArray[iNeedIndex].IsOK(IsOnlyUseSoul()));
			iItemNo = CRYSTAL_ITEM_NO_BASE+(AttachElement()*10);
		} break;	//ȯȥ
	case RIT_PROBABILITY:	{ bGray = (SItemPos::NullData() == m_kProbabilityItemPos)?(true):(false);	}break;	//�� �Ӽ�
	case RIT_INSUR_ITEM:	{ bGray = (SItemPos::NullData() == m_kInsureItemPos)?(true):(false);		}break;	//����
	default:
		{
			goto __HIDE;
		}
	}

	PgPlayer* pkPlayer = g_kPilotMan.GetPlayerUnit();
	if(!pkPlayer){ return; }

	PgInventory* pkInv = pkPlayer->GetInven();
	if(!pkInv){ return; }

	if( !bGray && RIT_INSUR_ITEM == iNeedIndex )
	{
		PgBase_Item kItem;
		if( S_OK != pkInv->GetItem(m_kInsureItemPos, kItem) )
		{
			return;
		}
		iItemNo = kItem.ItemNo();
	}
	else
	{
		iItemNo = m_kItemArray[iNeedIndex].iItemNo;
	}
	
	m_kItemArray[iNeedIndex].iNowNum = (true == bGray)?(0):(pkInv->GetTotalCount(iItemNo));
	kString = __min(m_kItemArray[iNeedIndex].iNowNum, m_kItemArray[iNeedIndex].iNeedNum);
	if( bGray )
	{
		kString+=L"/";
		kString+=m_kItemArray[iNeedIndex].iNeedNum;
	}

	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(iItemNo);
	if(pItemDef)
	{
		g_kUIScene.RenderIcon(pItemDef->ResNo(), pWnd->TotalLocation(), false, 40, 40, bGray);
	}
	pWnd->SetCustomData(&(iItemNo), sizeof(iItemNo));
	return;
	
__HIDE:
	{
		pWnd->SetCustomData(&iNull, sizeof(iNull));
		pWnd->Text(std::wstring(L""));
	}
}

void PgItemRarityUpgradeMgr::DisplaySrcItem(XUI::CXUI_Wnd *pWnd)
{//���� �ʵ带 ã�Ƽ� �̸� ����. �������� ���ų� �ϸ� �ø����� ��� �޼����� ����.
	if (!pWnd) { return; }
	int const iNull = 0;

	std::wstring wstrName;
	POINT2 rPT;
	if(m_kItem.IsEmpty()){goto __HIDE;}
	if(!m_kItem.ItemNo()){goto __HIDE;}
	if(IsChangedGuid()){goto __HIDE;}//guid �ٲ������ ã�ư��簡.

	rPT =	pWnd->TotalLocation();

{
	GET_DEF(CItemDefMgr, kItemDefMgr);
	CItemDef const *pItemDef = kItemDefMgr.GetDef(m_kItem.ItemNo());
	if(pItemDef)
	{
		g_kUIScene.RenderIcon( pItemDef->ResNo(), rPT, false );
	}

	pWnd->SetCustomData(&m_kItem.ItemNo(), sizeof(m_kItem.ItemNo()));
}

	return;
__HIDE:
	{
		/*assert(pSrcNameWnd);
		if(pSrcNameWnd)
		{
			pSrcNameWnd->Text(TTW(59001));
		}*/
		pWnd->SetCustomData(&iNull, sizeof(iNull));
		m_kItem.Clear();
		m_kSrcItemPos.Clear();
	}
	return;
}

void PgItemRarityUpgradeMgr::DisplayResultItem(XUI::CXUI_Wnd *pWnd)
{
	XUI::CXUI_Wnd *pFormWnd = NULL;
	XUI::CXUI_Wnd *pShadowWnd = NULL;
	XUI::CXUI_Wnd *pSrcNameWnd = NULL;

	int const iNull = 0;

	if (!pWnd)
	{
		return;
	}
	pFormWnd = pWnd->Parent();
	assert(pFormWnd);
	if (!pFormWnd)
	{
		pWnd->SetCustomData(&iNull, sizeof(iNull));
		return;
	}
	if(pFormWnd)
	{
		pShadowWnd = pFormWnd->Parent();
		assert(pShadowWnd);
		if(pShadowWnd)
		{
			pSrcNameWnd = pShadowWnd->GetControl(_T("SFRM_DEST_NAME"));
		}
		else
		{
			pWnd->SetCustomData(&iNull, sizeof(iNull));
			return;
		}
	}

	std::wstring wstrName;
	POINT2 rPT;
	if(m_kItem.IsEmpty()){goto __HIDE;}
	if(!m_kItem.ItemNo()){goto __HIDE;}
	if(IsChangedGuid()){goto __HIDE;}//guid �ٲ������ ã�ư��簡.
	if(!m_kResultItem.ItemNo()){goto __HIDE;}

//		pWnd->Visible(true);

	rPT = pWnd->TotalLocation();
	int const iItemNo = m_kResultItem.ItemNo();
	bool bQVisible = true;
	{
		GET_DEF(CItemDefMgr, kItemDefMgr);
		CItemDef const *pItemDef = kItemDefMgr.GetDef(iItemNo);

		if(pItemDef)
		{
			g_kUIScene.RenderIcon( pItemDef->ResNo(), rPT, false );
			bQVisible = false;
		}
	}

	MakeItemName(iItemNo, m_kResultItem.EnchantInfo(), wstrName);
	pWnd->SetCustomData(&iItemNo, sizeof(iItemNo));
	
	XUI::CXUI_Wnd* pkQ = pWnd->GetControl(_T("IMG_Q"));
	if (pkQ)
	{
		pkQ->Visible(bQVisible);
		if (bQVisible)
		{
			int const iNull = 0;
			pWnd->SetCustomData(&iNull , sizeof(iNull ));
		}
	}


	assert(pSrcNameWnd);
	if(pSrcNameWnd)
	{
		pSrcNameWnd->Text(wstrName);
	}
	return;
__HIDE:
	{
		assert(pSrcNameWnd);
		if(pSrcNameWnd)
		{
			pSrcNameWnd->Text(_T(""));
			pWnd->SetCustomData(NULL, sizeof(iItemNo));
		}
		pWnd->SetCustomData(&iNull, sizeof(iNull));
		XUI::CXUI_Wnd* pkQ = pWnd->GetControl(_T("IMG_Q"));
		if (pkQ)
		{
			pkQ->Visible(false);
		}
		m_kResultItem.Clear();
	}
}

bool PgItemRarityUpgradeMgr::SendReqRarityUpgrade(bool bIsTrueSend)
{
	if(m_kItem.IsEmpty()){goto __FAILED;}
	if(!m_kItem.ItemNo()){goto __FAILED;}
	if(IsChangedGuid()){goto __FAILED;}//guid �ٲ������ ã�ư��簡.


	goto __SUCCESS;
	//�� �� �ִ� ���ٷ� �Ǻ� �ؾ� �ϴµ�.
__SUCCESS:
	{
		if(bIsTrueSend)
		{
			SPT_C_M_REQ_ITEM_RARITY_UPGRADE kStruct;
			kStruct.NpcGuid(m_kNpcGuid);
			kStruct.PropertyType(AttachElement()==E_PPTY_CURSE ? E_PPTY_NONE : AttachElement());
			kStruct.TargetItemPos(m_kSrcItemPos);
			kStruct.UseInsuranceItem(SItemPos::NullData() != m_kInsureItemPos);
			kStruct.InsuranceItemPos(m_kInsureItemPos);
			kStruct.UseSuccessRateItem(SItemPos::NullData() != m_kProbabilityItemPos);
			kStruct.SuccessRateItemPos(m_kProbabilityItemPos);
			BM::Stream kPacket;
			kStruct.WriteToPacket(kPacket);
			NETWORK_SEND(kPacket)
		}
		else
		{
			for (int i = 0; i < 3; ++i)
			{
				if(!m_kItemArray[i].IsOK(IsOnlyUseSoul()))
				{
					return false;
				}
			}
		}
		return true;
	}
__FAILED:
	{
		return false;
	}
}

int PgItemRarityUpgradeMgr::CheckNeedItem()
{
	for (int i = 0; i < RIT_INSUR_ITEM+1; ++i)
	{
		if (!m_kItemArray[i].IsOK(IsOnlyUseSoul()))
		{
			return m_kItemArray[i].iItemNo;
		}
	}

	return 0;
}

int const PgItemRarityUpgradeMgr::GetNowNeedItemCount(int const iNeed) const
{
//	if (MAX_ITEM_Rarity_UPGRADE_NEED_ARRAY+1 < iNeed || 0 > iNeed )
//	{
		return 0;
//	}
//	return __min(m_kItemArray[iNeed].iNowNum, m_kItemArray[iNeed].iNeedNum);
}

bool PgItemRarityUpgradeMgr::IsChangedGuid() const
{
	if (m_kItem.IsEmpty())
	{
		return true;
	}
	return (m_kItem.Guid() != m_guidSrcItem);
}

bool PgItemRarityUpgradeMgr::SetElementInfo(int const iIndex, PgBase_Item const & rkSItem, PgInventory *pInv, SNeedItemRarityUpgrade* pkArray)
{
	if (!pkArray)
	{
		return false;
	}
	
	DWORD dwItemNo = 0;
	E_ITEM_GRADE const Grade = GetItemGrade(rkSItem);
	switch(iIndex)
	{
	case RIT_SOUL:			{ dwItemNo = SOUL_ITEM_NO;			} break;
	case RIT_INSUR_ITEM:	{ dwItemNo = ISURANCE_ITEM_NO_BASE;	} break;
	case RIT_PROBABILITY:	
		{
			switch( Grade )
			{
			case IG_NORMAL:
			case IG_RARE:
			case IG_UNIQUE:
			case IG_ARTIFACT:
			case IG_LEGEND:
			case IG_EPIC:
				{
					dwItemNo = PROBABILITY_ITEM_NO_BASE + (Grade * 10);
				}break;
			default:
				{
					if( AttachElement()!=E_PPTY_CURSE )
					{
						return true;
					}
				}break;
			}
		} break;
	default:{} break;
	}

	PgPlayer* pkPlayer = g_kPilotMan.GetPlayerUnit();
	if(!pkPlayer)
	{
		return false;
	}
	int const iMaxItemNo = pInv->GetTotalCount(dwItemNo);
	int iNeed = 1;
	if( RIT_SOUL == iIndex )
	{
		iNeed = PgItemRarityUpgradeFormula::GetNeedSoulCount(Grade, rkSItem,pkPlayer);
		if(g_pkWorld && g_pkWorld->IsHaveAttr(GATTR_MYHOME))// ����Ȩ ���ο� ������
		{
			iNeed = static_cast<int>(static_cast<float>(iNeed) * ((100.0f - lwHomeUI::GetMyHomeSideJobDiscountRate(MSJ_SOULCRAFT, MSJRT_SOUL)) / 100.0f ));
		}
	}
	pkArray[iIndex].SetInfo(iIndex, dwItemNo, iNeed, iMaxItemNo);
	return true;
}

bool PgItemRarityUpgradeMgr::SetMaterialItem(EKindUIIconGroup const kGroup, SItemPos const& rkItemPos)
{
	PgPlayer* pkPlayer = g_kPilotMan.GetPlayerUnit();
	if(!pkPlayer){return false;}
	PgInventory *pInv = pkPlayer->GetInven();
	if(!pInv){return false;}

	PgBase_Item kItem;
	if(S_OK != pInv->GetItem(rkItemPos, kItem)){ return false; }

	switch( kGroup )
	{
	case KUIG_ITEM_RARITY_UPGRADE_INSURENCE:	
		{ 
			m_kInsureItemPos = rkItemPos;		 
			InsureItemNo( kItem.ItemNo() );
		} break;
	case KUIG_ITEM_RARITY_UPGRADE_PROBABILITY:	
		{
			m_kProbabilityItemPos = rkItemPos; 
		} break;
	default:
		{
			assert(0);
			return false;
		}
	}
	return true;
}

void PgItemRarityUpgradeMgr::SetItem(EKindUIIconGroup const kType, SIconInfo const & rkInfo)
{
	if(InProgress())
	{
		lwAddWarnDataTT(1482);
	}
	else if (0!=rkInfo.iIconGroup && 0<=rkInfo.iIconKey)
	{
		bool bSetItem = false;

		switch( kType )
		{
		case KUIG_ITEM_RARITY_UPGRADE_SRC:
			{	
				if( SetSrcItem(SItemPos(rkInfo.iIconGroup, rkInfo.iIconKey)) )
				{
					bSetItem = true;
					InitMaterialBtnState();
				}
			} break;
		case KUIG_ITEM_RARITY_UPGRADE_INSURENCE:
		case KUIG_ITEM_RARITY_UPGRADE_PROBABILITY:
			{				
				if( SetMaterialItem(kType, SItemPos(rkInfo.iIconGroup, rkInfo.iIconKey)) )
				{
					bSetItem = true;
				}
			}break;
		}

		if( bSetItem )
		{
			XUI::CXUI_Wnd* pWnd = XUIMgr.Get(_T("SFRM_ITEM_RARITY_UPGRADE"));
			if( pWnd )
			{
				XUI::CXUI_Wnd* pSrc = pWnd->GetControl(L"IMG_ITEM");
				if( pSrc )
				{
					pSrc->Visible(false);
				}
			}
		}
	}
}

void PgItemRarityUpgradeMgr::SetSrcMaterialBtnInit(XUI::CXUI_Wnd* pWnd, int const iType, bool const bVisible)
{
	if( !pWnd ){ return; }
	BM::vstring vStr(L"BTN_REG");
	vStr += iType;
	XUI::CXUI_Wnd* pReg = pWnd->GetControl(vStr);

	vStr = L"BTN_DEREG";
	vStr += iType;
	XUI::CXUI_Wnd* pDeReg = pWnd->GetControl(vStr);
	if( !pReg || !pDeReg ){ return; }
	pReg->Visible(bVisible);
	pDeReg->Visible(!pReg->Visible());
}

void PgItemRarityUpgradeMgr::SetMaterialItem(XUI::CXUI_Wnd* pWnd, int iType, bool bNoBuyMsg)
{
	if(InProgress())
	{
		lwAddWarnDataTT(1482);
		return;
	}

	if( !pWnd ){ return; }
	if( m_kItem.IsEmpty() ){ return; }

	PgPlayer* pkPlayer = g_kPilotMan.GetPlayerUnit();
	if(!pkPlayer){ return; }
	PgInventory *pkInv = pkPlayer->GetInven();
	if( !pkInv ){ return; }

	switch(g_kLocal.ServiceRegion())
	{
	case LOCAL_MGR::NC_DEVELOP:
		{
			bNoBuyMsg = false;
		}break;
	default:
		{
			bNoBuyMsg = true;
		}break;
	}

	EUseItemCustomType	eUICT_Type = UICT_NONE;
	DWORD dwItemNo = 0;
	SItemPos* pkTargetPos = NULL;
	switch( iType )
	{
	case RIT_INSUR_ITEM:
		{
			pkTargetPos = &m_kInsureItemPos;
			eUICT_Type = UICT_ENCHANT_INSURANCE;
		} break;
	case RIT_PROBABILITY:	
		{
			pkTargetPos = &m_kProbabilityItemPos;
			eUICT_Type = UICT_RARITY_SUCCESS;
		} break;
	}

	if( (*pkTargetPos) != SItemPos::NullData() )
	{
		pkTargetPos->Clear();
		SetSrcMaterialBtnInit(pWnd, iType, true);
		return;
	}

	if( iType == RIT_INSUR_ITEM )
	{
		ContHaveItemNoCount	rkItemCont;
		if( E_FAIL == pkInv->GetItems(eUICT_Type, rkItemCont) )
		{
			if( false == bNoBuyMsg )
			{
				lua_tinker::call<void, int, int>("OnCallStaticCashItemBuy", 0, 0);
			}
			else
			{
				lua_tinker::call<void, int, bool>("CommonMsgBoxByTextTable", 790401, true);
			}
			return;
		}

		ContHaveItemNoCount::iterator iter = rkItemCont.begin();
		while( iter != rkItemCont.end() )
		{
			ContHaveItemNoCount::key_type const& kItemNo = iter->first;
			if( !PgRarityUpgradeUIUtil::CheckRarityUseOKInsureItem( kItemNo ) )
			{
				iter = rkItemCont.erase(iter);
				continue;
			}
			++iter;
		}
				
		if( rkItemCont.size() > 1 )
		{
			UIItemUtil::CONT_CUSTOM_PARAM	kParam;
			auto ParamRst = kParam.insert(std::make_pair(std::wstring(L"CALL_UI"), UIItemUtil::EICUT_RARITY_UPGRADE));
			if( !ParamRst.second )
			{
				return;
			}
			UIItemUtil::CallCommonUseCustomTypeItems(rkItemCont, UIItemUtil::ECIUT_CUSTOM_DEFINED, kParam, UIItemUtil::CONT_CUSTOM_PARAM_STR());
			return;
		}
		else if( rkItemCont.size() == 1 )
		{
			dwItemNo = rkItemCont.begin()->first; 
		}
	}
	else if(iType == RIT_PROBABILITY)
	{
		E_ITEM_GRADE const Grade = GetItemGrade(m_kItem);
		dwItemNo = PROBABILITY_ITEM_NO_BASE + (Grade * 10);
	}
	else
	{
		return;
	}
	
	SItemPos	rkPos;
	if( S_OK == pkInv->GetFirstItem(dwItemNo, rkPos) )
	{
		SetMaterialItem(static_cast<ERarityItemType>(iType), rkPos);
		SetSrcMaterialBtnInit(pWnd, iType, false);
	}
	else
	{
		if( false == bNoBuyMsg )
		{
			switch( iType )
			{
			case RIT_PROBABILITY:	
				{
					lua_tinker::call<void, int, int>("OnCallStaticCashItemBuy", 2, GetItemGrade(m_kItem));
				} break;
			}
		}
		else
		{
			lua_tinker::call<void, int, bool>("CommonMsgBoxByTextTable", 790401, true);
		}
	}
}

void PgItemRarityUpgradeMgr::SetMaterialItem(ERarityItemType const eType, SItemPos kItemPos, bool bIsSlotUpdate)
{
	if( kItemPos.x == 0 && kItemPos.y == 0 )
	{
		return;
	}

	SIconInfo kInfo( kItemPos.x, kItemPos.y );
	EKindUIIconGroup kGroup = KUIG_NONE;

	switch( eType )
	{
	case RIT_INSUR_ITEM:	{ kGroup = KUIG_ITEM_RARITY_UPGRADE_INSURENCE;		} break;
	case RIT_PROBABILITY:	{ kGroup = KUIG_ITEM_RARITY_UPGRADE_PROBABILITY;	} break;
	}

	SetItem(kGroup, kInfo);
	if( bIsSlotUpdate )
	{
		XUI::CXUI_Wnd* pWnd = XUIMgr.Get(L"SFRM_ITEM_RARITY_UPGRADE");
		if( pWnd )
		{
			SetSrcMaterialBtnInit(pWnd, eType, false);
		}
	}
}

void PgItemRarityUpgradeMgr::MakeExplane(SNeedItemRarityUpgrade const * pkArray )
{
	if( !pkArray ) { return; }
	m_kExplane = L"{C=0xFF4D3413/}";

	m_kExplane+= CheckNeedItem() ? TTW(1471) : TTW(1470);

	for (int i = 0; i < RIT_INSUR_ITEM; ++i)
	{
		if ((IsOnlyUseSoul() && (i!=RIT_SOUL)) || i == RIT_PROBABILITY)
		{
			continue;
		}
		m_kExplane+=TTW(1472+i);
		SNeedItemRarityUpgrade &rkItem = m_kItemArray[i];
		if(!rkItem.IsOK(IsOnlyUseSoul()))
		{
			m_kExplane+=L"{C=0xFFFF0000/}";	
		}
		m_kExplane+=__min(rkItem.iNowNum,rkItem.iNeedNum);
		m_kExplane+=L"{C=0xFF4D3413/}";
		m_kExplane+=L"/";
		m_kExplane+=rkItem.iNeedNum;
		m_kExplane+=L"\n";
	}
}

BM::vstring PgItemRarityUpgradeMgr::GetExplane()	//����
{
	return m_kExplane;
}

void PgItemRarityUpgradeMgr::ResultProcess()
{
	if( IRUR_SUCCESS == m_RecentResult )
	{
		m_RecentResult = IRUR_NONE;

		GET_DEF(CItemDefMgr, kItemDefMgr);
		CItemDef const* pDef = kItemDefMgr.GetDef(m_kResultItem.ItemNo());
		if( !pDef )
		{
			return;
		}

		SEnchantInfo const& kEnchantInfo = m_kResultItem.EnchantInfo();
		int const iEquipPos = pDef->EquipPos();
		int const iLevelLimit = pDef->GetAbil(AT_LEVELLIMIT);
		SBasicOptionAmpKey const kAmpKey(GetEquipType(pDef), iLevelLimit, kEnchantInfo.BasicAmpLv());

		CONT_ENCHANT_ABIL kEnchantAbil;
		E_ITEM_GRADE Grade = ::GetItemGrade(m_kResultItem);
		E_ITEM_GRADE DiffGrade = PgItemRarityUpgradeMgr::GetGradeOfModifiedOption(
			m_kPastResultItem, m_kResultItem);
		
		if(IG_NORMAL < DiffGrade)
		{
			if(Grade!=DiffGrade)
			{
				Grade = DiffGrade;
			}
			m_kPastResultItem = m_kResultItem;
		}
		
		switch (Grade)
		{
		case IG_RARE:
			{
				::GetAbilObject(kEnchantInfo.BasicType1(), iEquipPos, kEnchantInfo.BasicLv1(), 0, 0, kEnchantAbil, kAmpKey);
			}break;
		case IG_UNIQUE:
			{
				::GetAbilObject(kEnchantInfo.BasicType2(), iEquipPos, kEnchantInfo.BasicLv2(), 0, 1, kEnchantAbil, kAmpKey);
			}break;
		case IG_ARTIFACT:
			{
				::GetAbilObject(kEnchantInfo.BasicType3(), iEquipPos, kEnchantInfo.BasicLv3(), 0, 2, kEnchantAbil, kAmpKey);
			}break;
		case IG_LEGEND:
			{
				::GetAbilObject(kEnchantInfo.BasicType4(), iEquipPos, kEnchantInfo.BasicLv4(), 0, 3, kEnchantAbil, kAmpKey);
			}break;
		case IG_EPIC:
			{
				::GetAbilObject(kEnchantInfo.BasicType5(), iEquipPos, kEnchantInfo.BasicLv5(), 0, 4, kEnchantAbil, kAmpKey);
			}break;
		default :
			break;
		}

		if( !kEnchantAbil.empty() )
		{//�ҿ� �ɼ��� ����
			BM::vstring vCraftStr(::MakeSlotAbilToolTipText(m_kResultItem, pDef, kEnchantAbil, true));
			vCraftStr += ::TTW(1234);// ����Ͽ����ϴ�.
			::Notice_Show(vCraftStr, EL_Normal, false);
		}

		if( IG_EPIC == Grade )
		{//�ְ� ����̴�
			Clear();
		}
	}
}

E_ITEM_GRADE PgItemRarityUpgradeMgr::GetGradeOfModifiedOption(PgBase_Item const& pastItem, PgBase_Item const& nowItem)
{
	if(nowItem.Guid()!=pastItem.Guid())
	{
		return IG_MIN;
	}

	if((nowItem.EnchantInfo().BasicLv1() != pastItem.EnchantInfo().BasicLv1())
		|| (nowItem.EnchantInfo().BasicType1() != pastItem.EnchantInfo().BasicType1()))
	{
		return IG_RARE;
	}
	else if((nowItem.EnchantInfo().BasicLv2() != pastItem.EnchantInfo().BasicLv2())
		|| (nowItem.EnchantInfo().BasicType2() != pastItem.EnchantInfo().BasicType2()))
	{
		return IG_UNIQUE;
	}
	else if((nowItem.EnchantInfo().BasicLv3() != pastItem.EnchantInfo().BasicLv3())
		|| (nowItem.EnchantInfo().BasicType3() != pastItem.EnchantInfo().BasicType3()))
	{
		return IG_ARTIFACT;
	}
	else if((nowItem.EnchantInfo().BasicLv4() != pastItem.EnchantInfo().BasicLv4())
		|| (nowItem.EnchantInfo().BasicType4() != pastItem.EnchantInfo().BasicType4()))
	{
		return IG_LEGEND;
	}
	else if((nowItem.EnchantInfo().BasicLv5() != pastItem.EnchantInfo().BasicLv5())
		|| (nowItem.EnchantInfo().BasicType5() != pastItem.EnchantInfo().BasicType5()))
	{
		return IG_EPIC;
	}
	return IG_MIN;
}