#ifdef _DEFAULT_INCLUDES
#include <AsDefault.h>
#endif

/******************************************************************************
 * usrlog.c
 *
 * Refactoring History
 * - 2026-03-24 / LeeDonghwan
 *   - log value/unit conversion readability cleanup
 *   - conversion flow consolidation
 *   - group comments added for quick readability
 *   - obsolete comments removed
 *   - repeated UserId equality checks reduced with range conditions and offsets
 ******************************************************************************/

#include <bur\plc.h>
#include <usr_log.h>
#include <visapi.h>
#include <VCLib.h>
#include "file.h"
#include <plastlib.h>
#include <asstring.h>
#include "defines.h"
#include "pic.h"
#include "asmath.h"
#include "lib_types.h"
#include "ma.h"
#include "mmi.h"
#include "rec.h"
#include "ipconfig.h"

typedef struct
{
	UINT text[S_USRLOG_DISPLAY_SIZE];
} logbook_typ;

typedef struct
{
	USINT initdiag;
	USINT resultdiag;
	USINT message;
	USINT initialize;
} logInit_typ;

typedef struct
{
	unsigned long Handle;
	UDINT uiDpId;
	UDINT uiIndex;
	UDINT uiDecimalPoint;
	UDINT uiDevide;
	UDINT uiDevide2;
	UDINT uiUnitGroupID;
	UDINT uiDispUnitID;
} dPoint_typ;

typedef enum
{
	LOG_RULE_BASE = 0,
	LOG_RULE_PERCENT,
	LOG_RULE_BAR_DIV10
} usrlog_rule_mode_typ;

typedef struct
{
	usrlog_rule_mode_typ mode;
	REAL svMax;
	char forcedUnit[10];
} usrlog_rule_typ;

#define STEP_IDLE 0
#define STEP_ACTIVE 1

#define BACKGROUND_COLOR 48
#define USERID_SOVPOSITION 1084

#define S_LOG_INIT_COMPLETE 0
#define S_LOG_INIT_ERROR 1
#define S_LOG_INIT_DELETING 2

#define S_CONV_MM2INCH 0.39370079
#define S_CONV_CEL2FAR 1.8
#define S_CONV_KN2USTON 11.24045
#define S_CONV_BAR2PSI 14.5037738
#define S_CONV_MPA2KG 1.019716

_GLOBAL UINT ACT_PIC;
_GLOBAL unsigned long vcHandle;
_GLOBAL UINT VCset_defUnit;
_GLOBAL mmi_typ MMI_DATA;
_GLOBAL_RETAIN rec_typ REC_DATA;
_GLOBAL_RETAIN cu_fix_typ CU_FIX;
_GLOBAL_RETAIN ej_fix_typ EJ_FIX;
_GLOBAL_RETAIN ea_fix_typ EA_FIX[S_MAX_NO_SIMPLE_AXES];
_GLOBAL_RETAIN iu_fix_typ IU_FIX;
_GLOBAL_RETAIN ip_fix_typ IP_FIX;
_GLOBAL_RETAIN ma_fix_typ MA_FIX;
_GLOBAL_RETAIN USINT ZoneName[S_MAX_NO_ZONES];
_GLOBAL USINT gActUser[10];
_GLOBAL_RETAIN ht_fix_typ HT_FIX;
_GLOBAL_RETAIN USINT HMI_Address[4];
_GLOBAL hmi_ipconfig_typ IPConfig;

_LOCAL UDINT EventCount;
_LOCAL UDINT hEvent;
_LOCAL UINT Status;
_LOCAL DINT EventType;
_LOCAL DINT UserId;
_LOCAL logENTRY_typ valueLog;
_LOCAL UINT sUnitShort[10];
_LOCAL UDINT lenUnitShort;

_LOCAL logINIT_typ initLog;
_LOCAL logREAD_typ readLog;
_LOCAL UDINT UsrLogOffset;
_LOCAL UINT offset_old;
_LOCAL UDINT actLogIdx;
_LOCAL UDINT oldLogIdx;

_LOCAL logbook_typ UsrLogDisplay[S_USRLOG_MAX_DISPLAY];
_LOCAL USINT UsrLogOld;
_LOCAL USINT UsrLogNew;

_LOCAL dPoint_typ dPoint;
_LOCAL UINT TEXTGRPIDX;
_LOCAL logInit_typ logInit;
_LOCAL UINT UsrLogPageNum;
_LOCAL UINT UsrLogMaxPage;
_LOCAL UINT tempUnitText;

static USINT usrlog_is_between(DINT value, DINT min, DINT max)
{
	return ((value >= min) && (value <= max)) ? 1 : 0;
}

static void usrlog_copy_unit_to_char(UINT* srcUnitShort, unsigned char* dstUnitText, USINT maxLen)
{
	USINT i;

	memset(dstUnitText, 0, maxLen);

	for (i = 0; i < maxLen; i++)
	{
		if (srcUnitShort[i] > '~')
			dstUnitText[i] = '~';
		else
			dstUnitText[i] = (unsigned char)srcUnitShort[i];

		if (dstUnitText[i] == '\0')
			break;
	}
}

static void usrlog_copy_char_to_unit(const char* srcUnitText, UINT* dstUnitShort, USINT maxLen)
{
	USINT i;

	memset(dstUnitShort, 0, sizeof(sUnitShort));

	for (i = 0; i < maxLen; i++)
	{
		if (srcUnitText[i] == '\0')
			break;

		dstUnitShort[i] = (UINT)((USINT)srcUnitText[i]);
	}
}

static void usrlog_get_base_conversion(unsigned char* charUnitText, REAL* ConvMulti, REAL* ConvOffset)
{
	*ConvOffset = 0;

	if (!strcmp((char*)charUnitText, " in"))
	{
		*ConvMulti = S_CONV_MM2INCH;
	}
	else if (!strcmp((char*)charUnitText, " in/s"))
	{
		*ConvMulti = S_CONV_MM2INCH;
	}
	else if (!strcmp((char*)charUnitText, " ~F"))
	{
		*ConvMulti = S_CONV_CEL2FAR;
		*ConvOffset = 32;
	}
	else if (!strcmp((char*)charUnitText, " Kg/cm~"))
	{
		*ConvMulti = S_CONV_MPA2KG;
	}
	else if (!strcmp((char*)charUnitText, " psi"))
	{
		*ConvMulti = S_CONV_BAR2PSI;
	}
	else
	{
		*ConvMulti = 1;
	}
}

static void usrlog_get_rule(DINT UserId, usrlog_rule_typ* rule)
{
	rule->mode = LOG_RULE_BASE;
	rule->svMax = 0;
	strcpy(rule->forcedUnit, "");

	/* =========================================
	   Clamp / Mold Close-Open speed related (%)
	   ========================================= */
	if (usrlog_is_between(UserId, 11, 13) || (UserId == 72) || (UserId == 74))
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = CU_FIX.NEG.SvMax1Vis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	if (usrlog_is_between(UserId, 35, 38))
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = CU_FIX.POS.SvMax1Vis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	/* =========================================
	   Pressure related (bar)
	   ========================================= */
	if ((usrlog_is_between(UserId, 27, 29) || (UserId == 63) || (UserId == 68)) ||
		(usrlog_is_between(UserId, 51, 54)) ||
		((UserId == 342) || (UserId == 345)) ||
		(usrlog_is_between(UserId, 126, 127) || (UserId == 136)) ||
		(usrlog_is_between(UserId, 111, 112) || (UserId == 133)) ||
		((UserId == 308) || (UserId == 316)) ||
		(usrlog_is_between(UserId, 214, 223)) ||
		((UserId == 273) || (UserId == 276)) ||
		(usrlog_is_between(UserId, 261, 263)) ||
		(usrlog_is_between(UserId, 182, 186)))
	{
		rule->mode = LOG_RULE_BAR_DIV10;
		strcpy(rule->forcedUnit, "bar");
		return;
	}

	/* =========================================
	   Ejector speed related (%)
	   ========================================= */
	if (usrlog_is_between(UserId, 116, 117) || (UserId == 134))
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = EJ_FIX.NEG.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	if (usrlog_is_between(UserId, 101, 102) || (UserId == 131))
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = EJ_FIX.POS.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	/* =========================================
	   Mold height speed related (%)
	   ========================================= */
	if (UserId == 341)
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = EA_FIX[S_MH].NEG.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	if (UserId == 344)
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = EA_FIX[S_MH].POS.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	/* =========================================
	   Nozzle speed related (%)
	   ========================================= */
	if (UserId == 301)
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = IU_FIX.NEG.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	if (UserId == 310)
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = IU_FIX.POS.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	/* =========================================
	   Injection / Decompression / Plastification
	   speed related (%)
	   ========================================= */
	if (usrlog_is_between(UserId, 152, 161) && (MA_FIX.OPT.SbInjSpeedPercent != 0))
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = IP_FIX.NEG.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	if ((UserId == 271) || (UserId == 274))
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = IP_FIX.NS.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}

	if (usrlog_is_between(UserId, 241, 243))
	{
		rule->mode = LOG_RULE_PERCENT;
		rule->svMax = IP_FIX.PL.SvMaxVis;
		strcpy(rule->forcedUnit, "%");
		return;
	}
}

static void usrlog_apply_conversion(DINT UserId, REAL* NewValue, REAL* OldValue, UINT* pUnitShort, UDINT decimalPoint)
{
	unsigned char charUnitText[10];
	REAL ConvMulti;
	REAL ConvOffset;
	REAL round_buf;
	usrlog_rule_typ rule;

	usrlog_get_rule(UserId, &rule);

	if (rule.mode == LOG_RULE_PERCENT)
	{
		if (rule.svMax != 0)
		{
			*NewValue = *NewValue * 100 / rule.svMax;
			*OldValue = *OldValue * 100 / rule.svMax;
		}

		usrlog_copy_char_to_unit(rule.forcedUnit, pUnitShort, 10);
		return;
	}

	if (rule.mode == LOG_RULE_BAR_DIV10)
	{
		*NewValue = *NewValue / 10;
		*OldValue = *OldValue / 10;

		usrlog_copy_char_to_unit(rule.forcedUnit, pUnitShort, 10);
		return;
	}

	dPoint.uiDevide = pow(10.0f, (double)decimalPoint);
	dPoint.uiDevide2 = (UserId == USERID_SOVPOSITION)
		? pow(10.0f, (double)decimalPoint - 1)
		: pow(10.0f, (double)decimalPoint);

	usrlog_copy_unit_to_char(pUnitShort, charUnitText, 10);
	usrlog_get_base_conversion(charUnitText, &ConvMulti, &ConvOffset);

	if (dPoint.uiDevide != 0)
	{
		*NewValue = *NewValue * ConvMulti / dPoint.uiDevide + ConvOffset;

		round_buf = *OldValue * ConvMulti;
		round_buf = (UserId == USERID_SOVPOSITION)
			? round0(round_buf * 10.0f)
			: round0(round_buf);

		*OldValue = round_buf / dPoint.uiDevide + ConvOffset;
	}
}

void _INIT usrlog_init(void)
{
	initLog.nrEntries = S_USRLOG_ENTRIES;
	initLog.nrStack = 100;
	initLog.name = gActUser;
	logINIT(&initLog);

	readLog.MaxLen = sizeof(UsrLogDisplay[0]);
	readLog.pFormat = "Y/M/D h:m:s Tp T1 T2 V U N";
	readLog.NrLines = S_USRLOG_MAX_DISPLAY;
	readLog.TypeTxtgrp = S_TXT_GRP_LOGTYPE;

	valueLog.Type = S_USRLOG_TYPE_CHGREC;
	valueLog.pText = 0;
	valueLog.pWcText = 0;
	valueLog.pUnitText = sUnitShort;
	valueLog.TxtGrpIdx = 0x80000000;

	logInit.initdiag = 3;
	logInit.resultdiag = 3;
	logInit.initialize = 0;
}

void _CYCLIC usrlog_cyclic(void)
{
	USINT i;
	USINT idx;
	USINT zone;
	UINT Modbus_Bit[] = {128,64,32,16,8,4,2,1};
	UINT Tcp_Bit[] = {2048,1024,512,256,128,64,32,16,8,4,2,1};

	VCEV_Count((UDINT)&EventCount);

	while (EventCount > 0)
	{
		Status = VCEV_Pop((UDINT)&hEvent);
		if ((Status != 0) || (hEvent == 0))
		{
			break;
		}

		EventCount--;

		Status = VCEV_I32Get(hEvent, (UDINT)"event-type", (UDINT)&EventType);

		switch (EventType)
		{
			case 0x1001:
				Status = VCEV_I32Get(hEvent, (UDINT)"pv-userid", (UDINT)&UserId);

				if ((Status == 0) && (UserId > 0) && (UserId < 20000))
				{
					lenUnitShort = sizeof(sUnitShort) / 2;
					memset(sUnitShort, 0, sizeof(sUnitShort));
					Status = VCEV_Utf16Get(hEvent, (UDINT)"displayunit-stext", (UDINT)sUnitShort, (UDINT)&lenUnitShort);

					memset(&dPoint, 0, sizeof(dPoint));
					Status = VCEV_I32Get(hEvent, (UDINT)"event-created-datetime", (UDINT)&valueLog.DateTime);
					Status = VCEV_FltGet(hEvent, (UDINT)"value-new", (UDINT)&valueLog.NewValue);
					Status = VCEV_FltGet(hEvent, (UDINT)"value-old", (UDINT)&valueLog.OldValue);
					Status = VCEV_I32Get(hEvent, (UDINT)"value-decimalplaces", (UDINT)&dPoint.uiDecimalPoint);

					usrlog_apply_conversion(
						UserId,
						&valueLog.NewValue,
						&valueLog.OldValue,
						sUnitShort,
						dPoint.uiDecimalPoint
					);

					/* =========================================
					   Heater-related setting changes
					   ========================================= */
					if ((UserId >= 430 && UserId <= 441) ||
						(UserId >= 450 && UserId <= 461) ||
						(UserId >= 470 && UserId <= 481) ||
						(UserId == 1700) ||
						(UserId == 1701) ||
						(UserId == 1146))
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
					}

					/* =========================================
					   External temperature zone name changes
					   Zone 9 ~ 20 (UserId 1800 ~ 1811)
					   ========================================= */
					if (UserId >= 1800 && UserId <= 1811)
					{
						zone = (USINT)(9 + (UserId - 1800));

						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[zone].NameIndex = HT_FIX.ZoneName[zone];
					}

					/* =========================================
					   CMS communication mode change
					   ========================================= */
					if (UserId == 150)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;

						REC_DATA.HC.EXTOnoff = 0;
						REC_DATA.HC.EXTOnoff_1 = 0;

						if (valueLog.NewValue == 1)
						{
							for (i = 9; i <= 20; i++)
							{
								if ((i <= 16) && (REC_DATA.HC.Zone[i].SbOn == 1))
									REC_DATA.HC.EXTOnoff += Modbus_Bit[i - 9];
								else if ((i >= 17) && (i <= 20) && (REC_DATA.HC.Zone[i].SbOn == 1))
									REC_DATA.HC.EXTOnoff_1 += Modbus_Bit[i - 17];
							}
						}
						else if (valueLog.NewValue == 2)
						{
							for (i = 9; i <= 20; i++)
							{
								if (REC_DATA.HC.Zone[i].SbOn == 1)
									REC_DATA.HC.EXTOnoff += Tcp_Bit[i - 9];
							}
						}
					}

					/* =========================================
					   External temperature selection
					   Zone 9 ~ 20 (UserId 510 ~ 521)
					   ========================================= */
					if (UserId >= 510 && UserId <= 521)
					{
						idx = (USINT)(UserId - 510);

						MMI_DATA.DatCpy.HeaterDataChanged = 1;

						if (MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if (idx <= 7)
							{
								if (valueLog.NewValue == 1)
									REC_DATA.HC.EXTOnoff += Modbus_Bit[idx];
								else
									REC_DATA.HC.EXTOnoff -= Modbus_Bit[idx];
							}
							else
							{
								if (valueLog.NewValue == 1)
									REC_DATA.HC.EXTOnoff_1 += Modbus_Bit[idx - 8];
								else
									REC_DATA.HC.EXTOnoff_1 -= Modbus_Bit[idx - 8];
							}
						}
						else if (MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if (valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[idx];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[idx];
						}
					}

					/* =========================================
					   HMI IP address synchronization
					   UserId 600 ~ 603
					   ========================================= */
					if (UserId >= 600 && UserId <= 603)
					{
						idx = (USINT)(UserId - 600);
						HMI_Address[idx] = IPConfig.IPAddress[idx];
					}

					valueLog.TxtIdx = UserId;
					logENTRY(&valueLog);
				}
				break;

			case 0x1002:
				break;
		}

		Status = VCEV_Free(hEvent);
	}

	logSAVE();

	if (readLog.VChandle == 0)
	{
		readLog.VChandle = VA_Setup(1, D_VC_NAME);
		return;
	}

	if (ACT_PIC == S_PIC_LOGBOOK)
	{
		if (UsrLogOld)
		{
			UsrLogOld = 0;

			if (UsrLogOffset < (S_USRLOG_ENTRIES - S_USRLOG_MAX_DISPLAY))
				UsrLogOffset += S_USRLOG_MAX_DISPLAY;
			else
				UsrLogOffset = S_USRLOG_ENTRIES - S_USRLOG_MAX_DISPLAY;
		}
		else if (UsrLogNew)
		{
			UsrLogNew = 0;

			if (UsrLogOffset > S_USRLOG_MAX_DISPLAY)
				UsrLogOffset -= S_USRLOG_MAX_DISPLAY;
			else
				UsrLogOffset = 0;
		}

		actLogIdx = logCNT();

		if ((UsrLogOffset != offset_old) || ((UsrLogOffset == 0) && (actLogIdx != oldLogIdx)))
		{
			readLog.pTarget = UsrLogDisplay[0].text;
			readLog.Idx = UsrLogOffset;
			logREAD(&readLog);

			if (readLog.status == 0)
			{
				offset_old = UsrLogOffset;
				oldLogIdx = actLogIdx;
			}
		}

		UsrLogMaxPage = S_USRLOG_ENTRIES / S_USRLOG_MAX_DISPLAY;
		UsrLogPageNum = UsrLogOffset / S_USRLOG_MAX_DISPLAY + 1;
	}
	else
	{
		offset_old = (UINT)-1;
	}

	if (logInit.initialize)
	{
		logInit.initialize = 0;
		logInit.initdiag = 3;
		logInit.resultdiag = 0;
		logInit.message = S_LOG_INIT_DELETING;

		if (!logModInit())
			logInit.message = S_LOG_INIT_COMPLETE;
		else
			logInit.message = S_LOG_INIT_ERROR;
	}
}
