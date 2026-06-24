#ifdef _DEFAULT_INCLUDES
 #include <AsDefault.h>
#endif
/******************************************************************************/
/* Visual Source Safe Header                                                  */
/*----------------------------------------------------------------------------*/
/* _Workfile:                                                               _ */
/* _Author:                                                                 _ */
/* _Date:                                                                   _ */
/* _Revision:                                                               _ */
/******************************************************************************/
/******************************************************************************/
/*       Task for copying the general machine configuration                   */
/*----------------------------------------------------------------------------*/
/*       Taskname:     usrlog                                                 */
/*       Filename:     usrlog.c                                               */
/*       Author:       B&R                                                    */
/*       Date:         24.07.2001                                             */
/*       Version:      V1.00.0                                                */
/******************************************************************************/
/***************************************************************************************
**                              include files                                         **
***************************************************************************************/
#include <bur\plc.h>
#include <usr_log.h>
#include <visapi.h>
#include <VCLib.h>
#include "file.h"
#include <plastlib.h>		/*20111230 JK to use round0*/
#include <asstring.h>		/*20111230 JK change from "string.h" to <string.h>*/
#include "defines.h"
#include "pic.h"
#include "asmath.h"	/*<math.h>*/	/*20090206 for calculate pow() JK*/
#include "lib_types.h"		/*20160314 LWH*/
#include "ma.h"				/*20160314 LWH*/
#include "mmi.h"			/*20170810 LJS*/
#include "rec.h"			/*20170810 LJS*/
#include "ipconfig.h"
/***************************************************************************************
**                          typedefs                                                  **
***************************************************************************************/
typedef struct{
	UINT		text[S_USRLOG_DISPLAY_SIZE];
}logbook_typ;

typedef struct{
	USINT	initdiag;
	USINT	resultdiag;
	USINT	message;
	USINT	initialize;		
}logInit_typ;
/***************************************************************************************
**                          defines                                                   **
***************************************************************************************/
#define		STEP_IDLE			0
#define		STEP_ACTIVE			1

#define		BACKGROUND_COLOR	48				/* Background color of logbook-output	*/
#define		USERID_SOVPOSITION	1084		/*20091229 sov position use ccm2 in unit group volume JK*/

#define		S_LOG_INIT_COMPLETE	0
#define		S_LOG_INIT_ERROR	1
#define		S_LOG_INIT_DELETING 2

#define		S_CONV_MM2INCH		0.39370079		/*20111230 JK to fix coversion error for mm to inch*/
#define		S_CONV_CEL2FAR		1.8
#define		S_CONV_KN2USTON		11.24045
#define		S_CONV_BAR2PSI		14.5037738		/*20160314 LWH	14.5037738*/
#define		S_CONV_MPA2KG		1.019716
/***************************************************************************************
**                          general pcc-global structures                             **
***************************************************************************************/
_GLOBAL		UINT    			ACT_PIC;
_GLOBAL		unsigned long		vcHandle;
_GLOBAL		UINT				VCset_defUnit;					/*20100327 default unit JK*/
_GLOBAL		mmi_typ				MMI_DATA;						/* HMI Data interface */ /*20170810 LJS �߰� */
_GLOBAL_RETAIN     rec_typ  			REC_DATA;		/* Recipe data of machine */ /*20170810 LJS �߰� */
_GLOBAL_RETAIN		cu_fix_typ  		CU_FIX;			/* 20160314 LWH Hydraulic ejector FIX-Data */
_GLOBAL_RETAIN		ej_fix_typ  		EJ_FIX;			/* 20160314 LWH Hydraulic ejector FIX-Data */
_GLOBAL_RETAIN     ea_fix_typ 			EA_FIX[S_MAX_NO_SIMPLE_AXES];	/* 20160314 LWH Easy axes FIX-Data */
_GLOBAL_RETAIN		iu_fix_typ  		IU_FIX;			/* 20160314 LWH Hydraulic ejector FIX-Data */
_GLOBAL_RETAIN		ip_fix_typ  		IP_FIX;			/* 20160314 LWH Hydraulic injection FIX-Data */
_GLOBAL_RETAIN		ma_fix_typ			MA_FIX;			/* 20160314 LWH Machine fix data */
_GLOBAL_RETAIN		USINT			ZoneName[S_MAX_NO_ZONES];	/*20110114 cumstom zone name display in user page JK*//* 20151016 PJS _LOCAL -> _GLOBAL */ /*20170810 LJS ���� �߰� */
_GLOBAL		USINT			   	gActUser[10];		/* Actual User-name *//*20180202 JSH logbook �� ���� ����� �̸��� ǥ���ϱ� ���� �������� ����. ���������� ����define �ؾ� ������ �����Ƽ� �׳� ����. usrlog.c ������ �׳� 10���� ��������. �����Ұ�*/
_GLOBAL_RETAIN   	ht_fix_typ			HT_FIX;						/* Heating fix data */
_GLOBAL_RETAIN		USINT			HMI_Address[4];					/* 20200703 LJS HMI Address Copy */
_GLOBAL				hmi_ipconfig_typ	IPConfig;
/***************************************************************************************
**                          internal (local) structures                               **
***************************************************************************************/
/* variables for reading events */
_LOCAL	UDINT			EventCount;			/* Event counter */
_LOCAL	UDINT			hEvent;				/* Event handle */
_LOCAL	UINT			Status;				/* Function status */
_LOCAL	DINT			EventType;			/* Type of the incoming event */
_LOCAL	DINT			UserId;				/* User-ID of event-variable */
_LOCAL	logENTRY_typ	valueLog;
_LOCAL	UINT			sUnitShort[10];
_LOCAL	UDINT			lenUnitShort;
/* variables for reading logbook */
_LOCAL  logINIT_typ		initLog;			/* FUB Data: Init user log book */
_LOCAL	logREAD_typ		readLog;			/* FUB Data: read logbook entry */
_LOCAL	UDINT			UsrLogOffset;
_LOCAL	UINT			offset_old;			/* marker for old offset 		*/
_LOCAL	UDINT			actLogIdx;			/* actual logging index 		*/
_LOCAL	UDINT			oldLogIdx;			/* last logging index 			*/
/* To be accessed by VC */
_LOCAL	logbook_typ		UsrLogDisplay[S_USRLOG_MAX_DISPLAY];	/* Display of user log book */
_LOCAL	USINT			UsrLogOld;			/* Inc. UsrLogOffset			*/
_LOCAL	USINT			UsrLogNew;			/* Dec. UsrLogOffset			*/

typedef struct{
	unsigned long	Handle;	/*20090206 Handle for VCDP JK*/
	UDINT			uiDpId;	/*20090206 Datapoint ID JK*/
	UDINT			uiIndex;	/*20100115 Index converted from udDpId JK*/
	UDINT			uiDecimalPoint;	/*20090206 Decimal point of datapoint JK*/
	UDINT			uiDevide;		/*20090206 Devide value JK*/
	UDINT			uiDevide2;
	UDINT			uiUnitGroupID;		/*20091229 unit group id JK*/
	UDINT			uiDispUnitID;	/*20091229 display unit id JK*/
}dPoint_typ;
_LOCAL		dPoint_typ	dPoint;		/*20090206 Structure for calc decimal point JK*/
_LOCAL		UINT	TEXTGRPIDX;	/*DELETE THIS AFTER TEST*/
_LOCAL		logInit_typ	logInit;	/*20111011 log initialize JK*/
_LOCAL		UINT		UsrLogPageNum;	/*user log page number*/
_LOCAL		UINT		UsrLogMaxPage;	/*user log max page number*/

_LOCAL		UINT		tempUnitText;

/**************************************************************************************
****                    I N I T U P  O F   T H E   T A S K                         ****
**************************************************************************************/
void _INIT usrlog_init(void)
{
	initLog.nrEntries 	= S_USRLOG_ENTRIES;
	initLog.nrStack		= 100;
	initLog.name		= gActUser;	/*20180202 JSH ���� �α��� �Ǿ� �ִ� ����� �̸�*/	
	logINIT(&initLog);

	readLog.MaxLen		= sizeof(UsrLogDisplay[0]);
	readLog.pFormat 	=  "Y/M/D h:m:s Tp T1 T2 V U N";		/*20180202 JSH ���� �α��� �Ǿ� �ִ� ����� �̸� N �߰� 	*/
	readLog.NrLines		= S_USRLOG_MAX_DISPLAY;
	readLog.TypeTxtgrp	= S_TXT_GRP_LOGTYPE;

	valueLog.Type		= S_USRLOG_TYPE_CHGREC;
	valueLog.pText		= 0;
	valueLog.pWcText	= 0;
	valueLog.pUnitText	= sUnitShort;
	valueLog.TxtGrpIdx 	= 0x80000000;

/*close dialog window*/	
	logInit.initdiag = 3;
	logInit.resultdiag = 3;
	logInit.initialize = 0;
}  /* end of _INIT */

/**************************************************************************************
****                C Y C L I C   P A R T   O F   T H E   T A S K                  ****
**************************************************************************************/
void _CYCLIC usrlog_cyclic(void)
{
	unsigned char	charUnitText[10], i;					/*20111230 JK to fix conversion error while unit change*/
	REAL			ConvMulti, ConvOffset, round_buf;		/*20111230 JK to fix conversion error while unit change*/
	UINT Modbus_Bit[] = {128,64,32,16,8,4,2,1};
	UINT Tcp_Bit[] = {2048,1024,512,256,128,64,32,16,8,4,2,1};

	/* read values changed by VC */
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
			case 0x1001:	/* change of numeric process value */
				Status = VCEV_I32Get(hEvent, (UDINT)"pv-userid", (UDINT)&UserId);
				if ((Status == 0) && (UserId > 0) && (UserId < 20000))
				{
					lenUnitShort = sizeof(sUnitShort) / 2;
					memset(sUnitShort, 0, sizeof(sUnitShort));	/*20090209 to reset unit text for normal counter or number without unit JK*/
					Status = VCEV_Utf16Get(hEvent, (UDINT)"displayunit-stext", (UDINT)sUnitShort, (UDINT)&lenUnitShort);
					
					memset(&dPoint, 0, sizeof(dPoint)); /*20100115 to correct decimal point, because no decimal value when set dropdown object JK*/
					Status = VCEV_I32Get(hEvent, (UDINT)"event-created-datetime", (UDINT)&valueLog.DateTime);
					Status = VCEV_FltGet(hEvent, (UDINT)"value-new", (UDINT)&valueLog.NewValue);
					Status = VCEV_FltGet(hEvent, (UDINT)"value-old", (UDINT)&valueLog.OldValue);
					/*20090206 for display decimal point JK*/
					/*Status = VCEV_I32Get(hEvent, (UDINT)"textgroup-id", (UDINT)&TEXTGRPIDX);*/
					Status = VCEV_I32Get(hEvent, (UDINT)"value-decimalplaces", (UDINT)&dPoint.uiDecimalPoint);
					/*
					Status = VCDP_Initialize((UDINT)&dPoint.Handle);
					Status = VCEV_I32Get(hEvent, (UDINT)"pv-id", (UDINT)&dPoint.uiDpId);
					Status = VCDP_DPID2Index((UDINT)dPoint.Handle, (UDINT)dPoint.uiDpId, (UDINT)&dPoint.uiIndex);
					Status = VCDP_GetDecimalPlaces((UDINT)dPoint.Handle, (UDINT)dPoint.uiIndex, (UDINT)&dPoint.uiDecimalPoint);
					*/
					/*Status = VCEV_I32Get(hEvent, (UDINT)"unitgroup-name", (UDINT)&dPoint.uiUnitGroupID);*/
					/*Status = VCEV_I32Get(hEvent, (UDINT)"displayunit-stext", (UDINT)&dPoint.uiDispUnitID);*/
					dPoint.uiDevide		= pow(10.0f, (double)dPoint.uiDecimalPoint);
					dPoint.uiDevide2	= (UserId == USERID_SOVPOSITION) ? pow(10.0f, (double)dPoint.uiDecimalPoint - 1) : pow(10.0f, (double)dPoint.uiDecimalPoint);
					
					/*20111230 JK to fix conversion error while unit change*/
					strcpy(charUnitText, "");
					for (i=0; i<10; i++)
						{
						if (sUnitShort[i] > '~')	/*over 126th character, strcmp does not work properly*/
							charUnitText[i] = '~';	/*replace 126th character instead of over 126th char*/
						else
							charUnitText[i] = (unsigned char)sUnitShort[i];
						if (charUnitText[i] == '\0')
							break;
					}
					ConvOffset = 0;
					if 		(!strcmp(charUnitText, " in"))			ConvMulti = S_CONV_MM2INCH;
					else if (!strcmp(charUnitText, " in/s"))		ConvMulti = S_CONV_MM2INCH;
						else if (!strcmp(charUnitText, " ~F"))		{	ConvMulti = S_CONV_CEL2FAR;		ConvOffset = 32;}
							else if (!strcmp(charUnitText, " Kg/cm~"))		ConvMulti = S_CONV_MPA2KG;
								else if (!strcmp(charUnitText, " psi"))			ConvMulti = S_CONV_BAR2PSI;
									else 											ConvMulti = 1;
					
					if (dPoint.uiDevide != 0)
					{
						valueLog.NewValue = valueLog.NewValue * ConvMulti / dPoint.uiDevide + ConvOffset;
						/*round_buf = (long)(valueLog.OldValue * ConvMulti * dPoint.uiDevide) / (REAL)dPoint.uiDevide;*/	/*to cut off decimal point*//*uiDevide2*/
						round_buf = valueLog.OldValue * ConvMulti / dPoint.uiDevide + ConvOffset;
						/*
						round_buf = round0(round_buf * 10.0f);
						round_buf = (UserId == USERID_SOVPOSITION) ? round_buf : (long)round_buf / 10;
						*/
						round_buf = (UserId == USERID_SOVPOSITION) ? round0(round_buf * 10.0f) : round0(round_buf);
						valueLog.OldValue = round_buf / dPoint.uiDevide + ConvOffset;	/*uiDevide2*/
					}
					/*end of insert JK*/
					/*valueLog.OldValue = valueLog.NewValue;*/	/*20090206 change remark JK*/
					
					/*20160314 LWH	Mold Close  & Mold Open Logdata change*/
					if(UserId>=11 && UserId<=13 || UserId==72 || UserId==74)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / CU_FIX.NEG.SvMax1Vis;
						valueLog.OldValue = valueLog.OldValue * 1000 / CU_FIX.NEG.SvMax1Vis;
						strcpy(charUnitText, "%");
					}	
					if(UserId>=35 && UserId<=38)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / CU_FIX.POS.SvMax1Vis;
						valueLog.OldValue = valueLog.OldValue * 1000 / CU_FIX.POS.SvMax1Vis;
						strcpy(charUnitText, "%");
					}	
					if((UserId>=27 && UserId<=29 || UserId==63 || UserId==68) ||
						(UserId>=51 && UserId<=54) || (UserId==342 || UserId==345) ||
						(UserId>=126 && UserId<=127 || UserId==136) || (UserId>=111 && UserId<=112 || UserId==133) ||
						(UserId==342 || UserId==345) || (UserId==308 || UserId==316) ||
						(UserId>=214 && UserId<=223) || (UserId==273 || UserId==276) || (UserId>=261 && UserId<=263) ||
						(UserId>=182 && UserId<=186))
					{
						valueLog.NewValue = valueLog.NewValue / 10;
						valueLog.OldValue = valueLog.OldValue / 10;
						strcpy(charUnitText, "bar");
					}	
					/*20160314 LWH	Ejetor Forward & Ejector Backward Logdata change*/
					if(UserId>=116 && UserId<=117 || UserId==134)				
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / EJ_FIX.NEG.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / EJ_FIX.NEG.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					if(UserId>=101 && UserId<=102 || UserId==131)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / EJ_FIX.POS.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / EJ_FIX.POS.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					/*20160314 LWH	Mold Height Logdata change*/
					if(UserId==341)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / EA_FIX[S_MH].NEG.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / EA_FIX[S_MH].NEG.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					if(UserId==344)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / EA_FIX[S_MH].POS.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / EA_FIX[S_MH].POS.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					/*20160314 LWH	Nozzle Logdata change*/
					if(UserId==301)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / IU_FIX.NEG.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / IU_FIX.NEG.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					if(UserId==310)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / IU_FIX.POS.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / IU_FIX.POS.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					/*20160314 LWH	Injection & Decompression & Plastification Logdata change*/
					if(UserId>=152 && UserId<=161 && MA_FIX.OPT.SbInjSpeedPercent != 0)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / IP_FIX.NEG.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / IP_FIX.NEG.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					if(UserId==271 || UserId==274)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / IP_FIX.NS.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / IP_FIX.NS.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					if(UserId>=241 && UserId<=243)
					{
						valueLog.NewValue = valueLog.NewValue * 1000 / IP_FIX.PL.SvMaxVis;
						valueLog.OldValue = valueLog.OldValue * 1000 / IP_FIX.PL.SvMaxVis;
						strcpy(charUnitText, "%");
					}
					/*20170810 LJS ���� ���� �µ� ���� �� */
					if((UserId >= 430 && UserId <= 441) || (UserId >= 450 && UserId <= 461) ||
						(UserId >= 470 && UserId <= 481))
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
					}
					/*20170830 LJS �ܺοµ� ���� ���� �� */
					if(UserId == 1700)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
					}
					/*20180115 LJS FoolProof �������� ���� �� */
					if(UserId == 1701)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
					}
					/*20180115 LJS ���� ���귮 ���� ��*/
					if(UserId == 1146)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
					}
					/*20170810 LJS PID �ܺοµ� �̸� ���� �� */
					if(UserId == 1800)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[9].NameIndex = HT_FIX.ZoneName[9];
					}			
					if(UserId == 1801)	
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[10].NameIndex = HT_FIX.ZoneName[10];
					}
					if(UserId == 1802)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[11].NameIndex = HT_FIX.ZoneName[11];
					}
					if(UserId == 1803)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[12].NameIndex = HT_FIX.ZoneName[12];
					}
					if(UserId == 1804)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[13].NameIndex = HT_FIX.ZoneName[13];
					}
					if(UserId == 1805)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[14].NameIndex = HT_FIX.ZoneName[14];
					}
					if(UserId == 1806)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[15].NameIndex = HT_FIX.ZoneName[15];
					}
					if(UserId == 1807)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[16].NameIndex = HT_FIX.ZoneName[16];
					}
					if(UserId == 1808)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[17].NameIndex = HT_FIX.ZoneName[17];
					}
					if(UserId == 1809)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[18].NameIndex = HT_FIX.ZoneName[18];
					}
					if(UserId == 1810)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[19].NameIndex = HT_FIX.ZoneName[19];
					}
					if(UserId == 1811)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						REC_DATA.HC.Zone[20].NameIndex = HT_FIX.ZoneName[20];
					}
					/*END*/
					/* 20191007 LJS CMS ��� ��� ���� �� */
					if(UserId == 150)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						REC_DATA.HC.EXTOnoff = 0;
						REC_DATA.HC.EXTOnoff_1 = 0;
						
						if(valueLog.NewValue == 1)
						{
							for(i=9; i<=20; i++)
							{
								if(i<=16 && REC_DATA.HC.Zone[i].SbOn == 1)
									REC_DATA.HC.EXTOnoff += Modbus_Bit[i-9];
								else if((i>=17 && i <= 20) && REC_DATA.HC.Zone[i].SbOn == 1)
									REC_DATA.HC.EXTOnoff_1 += Modbus_Bit[i-17];
							}
						}
						else if(valueLog.NewValue == 2)
						{
							for(i=9; i<=20; i++)
							{
								if(REC_DATA.HC.Zone[i].SbOn == 1)
									REC_DATA.HC.EXTOnoff += Tcp_Bit[i-9];
							}
						}
					}
					/*20170810 LJS �ܺοµ� ���� �� */	
					if(UserId == 510)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Modbus_Bit[0];
							else
								REC_DATA.HC.EXTOnoff -= Modbus_Bit[0];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[0];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[0];
						}
					}
					if(UserId == 511)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Modbus_Bit[1];
							else
								REC_DATA.HC.EXTOnoff -= Modbus_Bit[1];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[1];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[1];
						}
					}
					if(UserId == 512)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Modbus_Bit[2];
							else
								REC_DATA.HC.EXTOnoff -= Modbus_Bit[2];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[2];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[2];
						}
					}
					if(UserId == 513)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Modbus_Bit[3];
							else
								REC_DATA.HC.EXTOnoff -= Modbus_Bit[3];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[3];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[3];
						}
					}
					if(UserId == 514)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Modbus_Bit[4];
							else
								REC_DATA.HC.EXTOnoff -= Modbus_Bit[4];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[4];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[4];
						}
					}
					if(UserId == 515)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Modbus_Bit[5];
							else
								REC_DATA.HC.EXTOnoff -= Modbus_Bit[5];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[5];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[5];
						}
					}
					if(UserId == 516)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Modbus_Bit[6];
							else
								REC_DATA.HC.EXTOnoff -= Modbus_Bit[6];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[6];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[6];
						}
					}
					if(UserId == 517)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Modbus_Bit[7];
							else
								REC_DATA.HC.EXTOnoff -= Modbus_Bit[7];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[7];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[7];
						}
					}
					if(UserId == 518)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff_1 += Modbus_Bit[0];
							else
								REC_DATA.HC.EXTOnoff_1 -= Modbus_Bit[0];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[8];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[8];
						}
					}
					if(UserId == 519)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff_1 += Modbus_Bit[1];
							else
								REC_DATA.HC.EXTOnoff_1 -= Modbus_Bit[1];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[9];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[9];
						}
					}
					if(UserId == 520)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff_1 += Modbus_Bit[2];
							else
								REC_DATA.HC.EXTOnoff_1 -= Modbus_Bit[2];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[10];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[10];
						}
					}
					if(UserId == 521)
					{
						MMI_DATA.DatCpy.HeaterDataChanged = 1;
						
						if(MA_FIX.OPT.SbCMS_CommunicationMode == 1)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff_1 += Modbus_Bit[3];
							else
								REC_DATA.HC.EXTOnoff_1 -= Modbus_Bit[3];
						}
						else if(MA_FIX.OPT.SbCMS_CommunicationMode == 2)
						{
							if(valueLog.NewValue == 1)
								REC_DATA.HC.EXTOnoff += Tcp_Bit[11];
							else
								REC_DATA.HC.EXTOnoff -= Tcp_Bit[11];
						}
					}
					/* 20200723 LJS HMI IP */
					/* IP �ּ� ù��° �ڸ� */
					if(UserId == 600)
					{
						HMI_Address[0] = IPConfig.IPAddress[0];
					}
					/* IP �ּ� �ι�° �ڸ� */
					if(UserId == 601)
					{
						HMI_Address[1] = IPConfig.IPAddress[1];
					}
					/* IP �ּ� ����° �ڸ� */
					if(UserId == 602)
					{
						HMI_Address[2] = IPConfig.IPAddress[2];
					}
					/* IP �ּ� �׹�° �ڸ� */
					if(UserId == 603)
					{
						HMI_Address[3] = IPConfig.IPAddress[3];
					}
					/*END*/
					
					valueLog.TxtIdx = UserId;
					logENTRY(&valueLog);
				}
				break;

			case 0x1002:	/* change of string process value */
				break;
		}

		Status = VCEV_Free(hEvent);
	}

	/* save logbook to data-object */
	logSAVE();

	/* get handle to VC */
	if (readLog.VChandle == 0)
	{
		readLog.VChandle = VA_Setup(1 , D_VC_NAME);
		return;
	}

	/* copy user logbook to screen */
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
			readLog.Idx 	= UsrLogOffset;
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
		offset_old = -1;
	}

	/*20111011 module delete action JK*/	
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

}  /* end of _CYCLIC */


