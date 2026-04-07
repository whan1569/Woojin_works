#ifdef _DEFAULT_INCLUDES
#include <AsDefault.h>
#endif
/******************************************************************************
 * ht_org.c
 *
 * Refactoring History
 * - 2026-04-07 / LeeDonghwan
 *   - sensor type/channel handling logic refactored
 *   - ST11 condition restored for ATA492 channel split
 *   - channel selection changed from string-compare to index-based logic
 *   - duplicated sensor setting logic unified into common function
 *   - strtok parsing isolated into safe helper function
 *   - step control logic converted to enum-based state machine
 *   - unnecessary comments removed and code structure simplified
 *   - stability improved for multi-channel (Ch01/Ch02) handling
 ******************************************************************************/

#include <bur\plc.h>
#include <htLIB.h>
#include "defines.h"
#include "lib_types.h"
#include "mmi.h"
#include "mc.h"
#include "ma.h"
#include "ht.h"
#include "alarm.h"
#include "option.h"
#include "rec.h"
#include <AsIO.h>
#include <AsIOAcc.h>

#define MAX_TEMPCARD_LOCAL 30

typedef enum {
	STEP_INIT = 0,
	STEP_WRITE = 1,
	STEP_DONE = 2
} SensorStep;

void ht_sim(UINT tcTime);

_GLOBAL		ma_stat_typ			MA_STAT;
_GLOBAL_RETAIN		ma_fix_typ			MA_FIX;
_GLOBAL     mmi_typ             MMI_DATA;
_GLOBAL_RETAIN     ht_fix_typ          HT_FIX;
_GLOBAL     ht_par_typ          HT_PAR;
_GLOBAL		mo_dat_typ			MOdat[S_MAX_NO_MOTOR];
_GLOBAL     act_data_typ    	ACT_DATA;
_GLOBAL		au_par_typ			AUT_PAR;
_GLOBAL 	BOOL 				bReadEndFlag;
_GLOBAL 	UINT 				Tempcard_label[29];
_GLOBAL 	INT 				uTempCardCounter;
_GLOBAL 	STRING 				ModuleInformation_tempcard[30][MAX_MODULE_NUMBER_TEMP];
_GLOBAL_RETAIN 	USINT				actCardtype[S_MAX_NO_TEMPCARD];
_GLOBAL		BOOL 				DO_Cooling_Motor_Run;
_GLOBAL		io_typ				gIO;
_GLOBAL		BOOL				DO_HopperCool;
_GLOBAL_RETAIN     rec_typ  		REC_DATA;

_LOCAL      LCCounter_typ       cnt_base;
_LOCAL		htINIT_typ			ht_init;
_LOCAL		htCTRL_typ			ht_ctrl;

_LOCAL		short				groupStat[S_MAX_NO_GROUPS];
_LOCAL		short				zoneError[S_MAX_NO_ZONES];
_LOCAL		SINT				checkzonealarm;
_LOCAL		USINT				LayerControlHeatPage[4];

_LOCAL		short				HtZoStat[S_MAX_NO_ZONES];

_LOCAL		AsIOAccWrite_typ	AsIOAccWrite1;
_LOCAL		STRING				sDevName[50];
_LOCAL		STRING				sChName[50];
_LOCAL		unsigned short		uiStepSetSensor;

_LOCAL		unsigned short		uiAtProtTime[S_MAX_NO_ZONES];
_LOCAL		BOOL				tiktok;

_LOCAL		SINT				HtTypeChangeReq;
_LOCAL		UINT				uCounter;
_LOCAL		BYTE				SelCard;
_LOCAL		BYTE				CardsTypeIsNotUnified;
_LOCAL		STRING		    	sProgressCardType[15];
_LOCAL		USINT		    	uVisTempCardPopup;
_LOCAL		USINT				visSensorTypeChangeActiveLevel;
_LOCAL		BOOL				visSensorTypeChangeActive;

_LOCAL 		char*				tokenST;
_LOCAL		STRING				copyAddress[50];
_LOCAL		UDINT				valAT;
_LOCAL		UINT				colorST[S_MAX_NO_TEMPCARD];

static BOOL GetTokenST(const char* src, char* out)
{
	char tmp[50];
	char* t;

	if (src == 0 || out == 0)
		return 0;
	if (strlen(src) == 0)
		return 0;

	memset(tmp, 0, sizeof(tmp));
	strcpy(tmp, src);

	t = strtok(tmp, ".");
	if (!t) return 0;
	t = strtok(0, ".");
	if (!t) return 0;
	t = strtok(0, ".");
	if (!t) return 0;

	strcpy(out, t);
	return 1;
}

static void GetChannelNameByIndex(UINT index, const char* token, char* out)
{
	if (out == 0)
		return;

	if (token != 0 && !strcmp(token, "ST11") && MA_FIX.ARCONF.UseATA492 == 1)
	{
		if ((index % 2) == 1)
			strcpy(out, "Cfo_SensorTypeCh02");
		else
			strcpy(out, "Cfo_SensorTypeCh01");
	}
	else
	{
		strcpy(out, "ConfigOutput02");
	}
}

static UDINT GetSensorValueForCard(USINT curT, BOOL isATA492)
{
	if (isATA492)
	{
		switch (curT)
		{
			case 1: return 0x8000;
			case 2: return 0x8001;
			case 4: return 0x8002;
			case 3: return 0x8003;
			default: return 0x8000;
		}
	}

	return (UDINT)curT;
}

static BOOL PrepareSensorWriteByIndex(UINT index, USINT reqType)
{
	char token[10];
	char chName[50];
	USINT curT;
	BOOL isST11ATA492;

	if (index >= MAX_TEMPCARD_LOCAL)
		return 0;
	if (strlen(ModuleInformation_tempcard[index]) == 0)
		return 0;

	memset(token, 0, sizeof(token));
	memset(chName, 0, sizeof(chName));

	if (!GetTokenST(ModuleInformation_tempcard[index], token))
		return 0;

	isST11ATA492 = (!strcmp(token, "ST11") && MA_FIX.ARCONF.UseATA492 == 1);
	GetChannelNameByIndex(index, token, chName);

	curT = (reqType == 100) ? HT_FIX.SensorType[0] : HT_FIX.Cardtype[index];
	valAT = GetSensorValueForCard(curT, isST11ATA492);

	if (reqType == 100)
		HT_FIX.Cardtype[index] = HT_FIX.SensorType[0];

	AsIOAccWrite1.enable = 1;
	AsIOAccWrite1.pDeviceName = ModuleInformation_tempcard[index];
	AsIOAccWrite1.pChannelName = (unsigned long)chName;
	AsIOAccWrite1.value = valAT;

	return 1;
}

_INIT void ht_org_init (void)
{
	unsigned char i;

	ht_init.nrGroups	= S_MAX_NO_GROUPS;
	ht_init.nrZones		= S_MAX_NO_ZONES;
	ht_init.nrCGroups	= 0;
	ht_init.pZonePar	= (unsigned long)HT_PAR.Zone;
	ht_init.pGroupPar	= (unsigned long)HT_PAR.Group;
	ht_init.pZoneFix	= (unsigned long)HT_FIX.Zone;
	ht_init.pGroupFix	= (unsigned long)HT_FIX.Group;
	ht_init.pCMeasFix	= 0;
	htINIT(&ht_init);

	ht_ctrl.pReq	= MMI_DATA.Heat.Req;
	ht_ctrl.pLow	= MMI_DATA.Heat.Lowering;
	ht_ctrl.pRel	= MA_STAT.AbHeatOn;

	checkzonealarm = 0;
	uiStepSetSensor = 0;
	uCounter = 0;
	HtTypeChangeReq = 101;
	CardsTypeIsNotUnified = 0;

	if (HT_FIX.SensorType[0] != 0)
	{
		for (i = 0; i < S_MAX_NO_TEMPCARD; i++)
			{
			if (HT_FIX.Cardtype[i] == 0)
				HT_FIX.Cardtype[i] = HT_FIX.SensorType[0];
		}
	}
}

_CYCLIC void ht_org_cyclic (void)
{
	unsigned char	gr, zo, i, tempcardCnt;
	int				tempSensorTypeChanged;
	unsigned char 	str_temp[3];

	tempSensorTypeChanged = -1;

#ifdef D_SIMULATOR_MODE
	ht_sim(5000);
#endif

	htCTRL(&ht_ctrl);

	for (gr = 0; gr < S_MAX_NO_GROUPS; gr++)
		{
		groupStat[gr] = htGET(HEAT_GET_GRSTAT, gr);

		if (((MMI_DATA.Mode == MODE_SEMI_AUTO) || (MMI_DATA.Mode == MODE_AUTO)) && (groupStat[gr] == HEAT_STAT_LOTOL))
		{
			SET_HTGR_ALARM(gr, S_ALM_HTGR_LOTOL)
		}
		else if (groupStat[gr] != HEAT_STAT_LOTOL)
		{
			RESET_HTGR_ALARM(gr, S_ALM_HTGR_LOTOL)
		}

		if (((MMI_DATA.Mode == MODE_SEMI_AUTO) || (MMI_DATA.Mode == MODE_AUTO)) && (groupStat[gr] == HEAT_STAT_HITOL))
		{
			SET_HTGR_ALARM(gr, S_ALM_HTGR_HITOL)
		}
		else if (groupStat[gr] != HEAT_STAT_HITOL)
		{
			RESET_HTGR_ALARM(gr, S_ALM_HTGR_HITOL)
		}

		if ((groupStat[gr] == HEAT_STAT_HIMAX) && (checkzonealarm))
		{
			SET_HTGR_ALARM(gr, S_ALM_HTGR_HIMAX)
			switch (gr)
			{
				case S_HEAT_BARREL_GROUP:
				case S_HEAT_COOLING_GROUP:
				case S_HEAT_MOLD_GROUP:
					MMI_DATA.Heat.Req[S_HEAT_BARREL_GROUP] = HEAT_REQ_TURN_OFF;
					MMI_DATA.Heat.Req[S_HEAT_COOLING_GROUP] = HEAT_REQ_TURN_OFF;
					MMI_DATA.Heat.Req[S_HEAT_MOLD_GROUP] = HEAT_REQ_TURN_OFF;
					break;
				case S_HEAT_MOLD_TEMP_CON_GROUP:
					MMI_DATA.Heat.Req[S_HEAT_MOLD_TEMP_CON_GROUP] = HEAT_REQ_TURN_OFF;
					break;
			}
		}
		else
		{
			RESET_HTGR_ALARM(gr, S_ALM_HTGR_HIMAX)
		}

		if ((groupStat[gr] == HEAT_STAT_ERROR) && (checkzonealarm))
		{
			SET_HTGR_ALARM(gr, S_ALM_HTGR_ERROR)
		}
		else
		{
			RESET_HTGR_ALARM(gr, S_ALM_HTGR_ERROR)
		}

		MA_STAT.AbHeatOn[gr] = ((groupStat[gr] != HEAT_STAT_OFF) && (groupStat[gr] != HEAT_STAT_HIMAX));
	}

	if (groupStat[S_HEAT_BARREL_GROUP] == HEAT_STAT_INTOL)
		tiktok = ~tiktok;
	else
		tiktok = 0;

	for (zo = 0; zo < S_MAX_NO_ZONES; zo++)
		{
		zoneError[zo] = htGET(HEAT_GET_ZOERROR, zo);

		if (zoneError[zo] != 0 && HT_PAR.Zone[zo].Mode != 0)
		{
			if ((zoneError[zo] & HEAT_ERR_SENSOR_BREAK) != 0)		SET_HTZO_ALARM(zo, S_ALM_HTZO_SENSOR_BREAK)
			else									RESET_HTZO_ALARM(zo, S_ALM_HTZO_SENSOR_BREAK)

			if ((zoneError[zo] & HEAT_ERR_SENSOR_ILLEGAL) != 0)	SET_HTZO_ALARM(zo, S_ALM_HTZO_SENSOR_INVALID)
			else									RESET_HTZO_ALARM(zo, S_ALM_HTZO_SENSOR_INVALID)

			if ((zoneError[zo] & HEAT_ERR_TUNE_TEMP) != 0)		SET_HTZO_ALARM(zo, S_ALM_HTZO_TUNETEMP)
			else									RESET_HTZO_ALARM(zo, S_ALM_HTZO_TUNETEMP)

			if ((zoneError[zo] & HEAT_ERR_AUTOTUNE) != 0)		SET_HTZO_ALARM(zo, S_ALM_HTZO_TUNEERR)
			else									RESET_HTZO_ALARM(zo, S_ALM_HTZO_TUNEERR)

			if ((zoneError[zo] & HEAT_ERR_PID_CTRL) != 0)		SET_HTZO_ALARM(zo, S_ALM_HTZO_CTRLERR)
			else									RESET_HTZO_ALARM(zo, S_ALM_HTZO_CTRLERR)

			if ((zoneError[zo] & HEAT_ERR_ZONE_SC) != 0)		SET_HTZO_ALARM(zo, S_ALM_HZZO_SHORT)
			else									RESET_HTZO_ALARM(zo, S_ALM_HZZO_SHORT)

			if ((zoneError[zo] & (HEAT_ERR_FREEZE | HEAT_ERR_ZONE_BREAK)) != 0)	SET_HTZO_ALARM(zo, S_ALM_HTZO_ZONE_BREAK)
			else									RESET_HTZO_ALARM(zo, S_ALM_HTZO_ZONE_BREAK)

			if ((zoneError[zo] & HEAT_ERR_MAX_TEMP) != 0)
			{
				SET_HTZO_ALARM(zo, S_ALM_HZZO_MAXTEMP)
				MMI_DATA.Heat.Req[S_HEAT_BARREL_GROUP] = HEAT_REQ_TURN_OFF;
				MMI_DATA.Heat.Req[S_HEAT_COOLING_GROUP] = HEAT_REQ_TURN_OFF;
				MMI_DATA.Heat.Req[S_HEAT_MOLD_GROUP] = HEAT_REQ_TURN_OFF;
				MMI_DATA.Heat.Req[S_HEAT_MOLD_TEMP_CON_GROUP] = HEAT_REQ_TURN_OFF;
			}
			else
			{
				RESET_HTZO_ALARM(zo, S_ALM_HZZO_MAXTEMP)
			}

			if (zo > 8)
			{
				if ((zoneError[zo] & HEAT_ERR_SENSOR_BREAK) != 0)	SET_ALARM(S_ALARM_EXT_TEMP_BROKEN)
				else									RESET_ALARM(S_ALARM_EXT_TEMP_BROKEN)
			}

			checkzonealarm = 1;
		}
		else
		{
			memset(MMI_DATA.Alarm.HtZo[zo].Alm, 0, sizeof(MMI_DATA.Alarm.HtZo[zo].Alm));
			if (checkzonealarm)
			{
				if (MA_STAT.ABarrelGroupStatus != HEAT_STAT_OFF)
				{
					MMI_DATA.Heat.Req[S_HEAT_BARREL_GROUP] = HEAT_REQ_QUIT_ERR;
					MMI_DATA.Heat.Req[S_HEAT_COOLING_GROUP] = HEAT_REQ_QUIT_ERR;
					MMI_DATA.Heat.Req[S_HEAT_MOLD_GROUP] = HEAT_REQ_QUIT_ERR;
				}
				if (htGET(HEAT_GET_GRSTAT, S_HEAT_MOLD_TEMP_CON_GROUP) != HEAT_STAT_OFF)
					MMI_DATA.Heat.Req[S_HEAT_MOLD_TEMP_CON_GROUP] = HEAT_REQ_QUIT_ERR;
				checkzonealarm = 0;
			}
		}

		if (MA_FIX.OPT.MHC_Page_Enable == 2 && zo >= 9)
		{
		}
		else
		{
			HtZoStat[zo] = htGET(HEAT_GET_ZOSTAT, zo);
			if (HtZoStat[zo] == HEAT_STAT_INTOL)
				uiAtProtTime[zo] = HT_FIX.Group[S_HEAT_BARREL_GROUP].StRelease - htGET(HEAT_GET_ZOTOLCNT, zo);
			else if (HtZoStat[zo] == HEAT_STAT_OFF)
				uiAtProtTime[zo] = HT_FIX.Group[S_HEAT_BARREL_GROUP].StRelease;
		}
	}

	if ((ACT_DATA.Zone[7].AT > REC_DATA.HC.Zone[7].ST) && (REC_DATA.HC.Zone[7].SbOn))
		gIO.DO.HopperCooling = 1;
	else if ((ACT_DATA.Zone[7].AT < HT_PAR.Hopper.STCoolOff))
		gIO.DO.HopperCooling = 0;

	if (MA_STAT.ATOil > HT_PAR.Oil.STCoolOn)
		MA_STAT.AbOilCool = 1;
	else if (MA_STAT.ATOil < HT_PAR.Oil.STCoolOff)
		MA_STAT.AbOilCool = 0;

	if (MOdat[S_MOTOR_MAIN].AbOn == 1 && MA_STAT.AbMaintenance)
		gIO.DO.HydCoolingPump = 1;
	else
		gIO.DO.HydCoolingPump = 0;

	if (AUT_PAR.SbOilHeat)
	{
		if (MA_STAT.ATOil > HT_PAR.Oil.STHeatOff)
			gIO.DO.OilHeatingHeater = 0;
		else if (MA_STAT.ATOil < HT_PAR.Oil.STHeatOn)
			gIO.DO.OilHeatingHeater = 1;
	}
	else
		gIO.DO.OilHeatingHeater = 0;

	if (MA_STAT.ATOil > HT_FIX.Oil.STMax)
	{
		SET_ALARM(S_ALARM_OIL_TEMPERATURE_HIGH)
		RESET_ALARM(S_ALARM_OIL_TEMPERATURE_LOW)
	}
	else if (MA_STAT.ATOil < HT_FIX.Oil.STMin)
	{
		SET_ALARM(S_ALARM_OIL_TEMPERATURE_LOW)
		RESET_ALARM(S_ALARM_OIL_TEMPERATURE_HIGH)
	}
		else
		{
			RESET_ALARM(S_ALARM_OIL_TEMPERATURE_HIGH)
			RESET_ALARM(S_ALARM_OIL_TEMPERATURE_LOW)
		}

	if (HT_PAR.Zone[0].Mode >= 1)
		i = 0;
	else if (HT_PAR.Zone[1].Mode >= 1)
		i = 1;
		else if (HT_PAR.Zone[2].Mode >= 1)
			i = 2;
			else
				i = 3;

	for (zo = 0; zo < 4; zo++)
		{
		if (i == zo)
			LayerControlHeatPage[zo] = 0;
		else
			LayerControlHeatPage[zo] = 1;
	}

	if (MA_STAT.AbEmStop)
	{
		MMI_DATA.Heat.Req[S_HEAT_BARREL_GROUP] = HEAT_REQ_TURN_OFF;
		MMI_DATA.Heat.Req[S_HEAT_COOLING_GROUP] = HEAT_REQ_TURN_OFF;
		MMI_DATA.Heat.Req[S_HEAT_MOLD_GROUP] = HEAT_REQ_TURN_OFF;
	}

	if ((HtTypeChangeReq == 100) || (HtTypeChangeReq == 101))
	{
		if (bReadEndFlag)
		{
			switch (uiStepSetSensor)
			{
				case STEP_INIT:
					if (PrepareSensorWriteByIndex(uCounter, (USINT)HtTypeChangeReq))
						uiStepSetSensor = STEP_WRITE;
					else
						HtTypeChangeReq = 0;
					break;

				case STEP_WRITE:
					AsIOAccWrite(&AsIOAccWrite1);
					if (AsIOAccWrite1.status != ERR_FUB_BUSY)
						uiStepSetSensor = STEP_DONE;
					break;

				case STEP_DONE:
					if (AsIOAccWrite1.status == ERR_OK)
					{
						if (HtTypeChangeReq == 100)
							actCardtype[uCounter] = HT_FIX.SensorType[0];
						else
							actCardtype[uCounter] = HT_FIX.Cardtype[uCounter];

						uCounter++;
						uiStepSetSensor = STEP_INIT;

						if (uTempCardCounter <= uCounter)
						{
							HtTypeChangeReq = 0;
							uCounter = 0;
						}

						memset(sProgressCardType, 0, sizeof(sProgressCardType));
						memset(str_temp, 0, sizeof(str_temp));
						itoa(uCounter, (UDINT)str_temp, 10);
						sProgressCardType[0] = '(';
						sProgressCardType[1] = ' ';
						strcat((UDINT)sProgressCardType, (UDINT)str_temp);
						strcat((UDINT)sProgressCardType, " / ");
						memset(str_temp, 0, sizeof(str_temp));
						itoa((UDINT)uTempCardCounter, (UDINT)str_temp, 10);
						strcat((UDINT)sProgressCardType, (UDINT)str_temp);
						strcat((UDINT)sProgressCardType, " )");
					}
					else
					{
						uiStepSetSensor = STEP_INIT;
					}
					break;
			}
		}
	}
	else if (HtTypeChangeReq != 0)
	{
		switch (uiStepSetSensor)
		{
			case STEP_INIT:
				if (PrepareSensorWriteByIndex((UINT)(HtTypeChangeReq - 1), (USINT)HtTypeChangeReq))
					uiStepSetSensor = STEP_WRITE;
				else
					HtTypeChangeReq = 0;
				break;

			case STEP_WRITE:
				AsIOAccWrite(&AsIOAccWrite1);
				if (AsIOAccWrite1.status != ERR_FUB_BUSY)
					uiStepSetSensor = STEP_DONE;
				break;

			case STEP_DONE:
				if (AsIOAccWrite1.status == ERR_OK)
				{
					actCardtype[HtTypeChangeReq - 1] = HT_FIX.Cardtype[HtTypeChangeReq - 1];
					uiStepSetSensor = STEP_INIT;
					HtTypeChangeReq = 0;
				}
				else
				{
					uiStepSetSensor = STEP_INIT;
				}
				break;
		}

		CardsTypeIsNotUnified = 0;
		if (uTempCardCounter > 1)
		{
			for (uCounter = 0; uCounter < uTempCardCounter - 1; uCounter++)
				{
				if (HT_FIX.Cardtype[uCounter] != HT_FIX.Cardtype[uCounter + 1])
					CardsTypeIsNotUnified++;
			}
		}
		uCounter = 0;
		if (CardsTypeIsNotUnified > 0)
			CardsTypeIsNotUnified = 1;
		else
			HT_FIX.SensorType[0] = HT_FIX.Cardtype[0];
	}

	uVisTempCardPopup = (HtTypeChangeReq != 0) ? 0 : 3;
	if (bReadEndFlag == 1 && uTempCardCounter == 0)
		HtTypeChangeReq = 0;
}
