/******************************************************************************/
/*       TASK: creating custom arconfig and apply it to system                */
/*----------------------------------------------------------------------------*/
/*       Taskname:     dynArConfig                                            */
/*       Filename:     dynArConfig.c                                          */
/*       Author:       skpark                                                 */
/******************************************************************************/

#include <bur/plctypes.h>

#ifdef _DEFAULT_INCLUDES
#include <AsDefault.h>
#endif

#include "dynArConfig.h"
#include "cardDefinition.h"

_GLOBAL_RETAIN	ma_fix_typ		MA_FIX;							/* Machine FIX-Data */
_GLOBAL	unsigned char DynMachineStatus;
_GLOBAL_RETAIN	UINT			D_INJECTION_MOTOR;
_GLOBAL_RETAIN	UINT			D_CLAMP_MOTOR;

_GLOBAL			UINT    		ACT_PIC;			/* Actual picture number */

/* 20250903 HDE Add function auto searching */
_GLOBAL_RETAIN 	UINT 		gDefCardCnt;
_GLOBAL 	   	STRING 		ModuleInformation_addCard[MAX_DEF_CARD_CNT][MAX_DEF_CARD_NAME];
_GLOBAL			char		dummyPath[30];
_GLOBAL		   	UINT		uDefCardCounter;

_LOCAL int Dyn_Preset_Req;
_LOCAL int Dyn_Option_Req;

_LOCAL AsIOMMRemove_typ arIO_Remove;
_LOCAL AsIOMMCreate_typ arIO_Create;
_LOCAL DatObjInfo_typ datObj_Info;
_LOCAL DatObjCreate_typ datObj_Create;
_LOCAL DatObjMove_typ datObj_Move;
_LOCAL DatObjDelete_typ datObj_Delete;
_LOCAL NCGLACT_NCDA_CREATE_typ ncmap_Create;


_LOCAL FileCreate_typ FCreate;
_LOCAL FileWrite_typ FWrite;
_LOCAL FileClose_typ FClose;

_LOCAL dynAR_typ DYN_AR;
_LOCAL dynData_typ *DYN_DATA;
_LOCAL char *arConfigBuf;
_LOCAL char *ioMapBuf;
_LOCAL char *ncMapBuf;
_LOCAL DINT	CardIndex[S_MAX_DYN_ADD_CARD];
_LOCAL DINT	CardDefView[S_MAX_DYN_ADD_CARD];
_LOCAL DINT	CardDefViewMax[S_MAX_DYN_ADD_CARD];
_LOCAL DINT	CardDefViewMin[S_MAX_DYN_ADD_CARD];
_LOCAL DINT	CardTypeMax;
_LOCAL DINT	CardTypeMin;

int arNodeNum;
dynArAdd_typ arNode[DYN_AR_LEN_NODE];

int ncNodeNum;
dynNcAdd_typ ncNode[DYN_AR_LEN_NODE];

/********************************************************************
 * Create Node Function												*
 ********************************************************************/
 void createCustomNode() {
 
 	/* Now, only standard configuration is available. */
 	/* MA_FIX.ARCONF.ARMap = 1; 20200113 KDH */
 
 	/* Create ARConfig Node. */
	switch (MA_FIX.ARCONF.ARMap) {
		case 0:		/* Not Use */
			break;
		case 1:
			dynAR_Customize(&MA_FIX.ARCONF);
			dynAR_Customize_Record();
		default:
			break;
	}
	

	
	/* Create NCMapping Node. */
	switch (MA_FIX.ARCONF.NCMap) {
		case DYN_AR_NC_P21_STD :		/* 기본사양 */
			//insertACOPOS("8V1640.001-2","SL1.IF1.ST2",0,"8AC114.60-2");
			insertACOPOS("8V128M.001-2","SL1.IF1.ST2",0,"8AC131.60-1");
			insertNCNode("SPax[0]",2,"SPInit","8LSC75R15",0, "Powerlink", "SL1.IF1");
			break;
		case DYN_AR_NC_P21_M2 : // 모터2 사용시
			insertACOPOS("8V128M.001-2","SL1.IF1.ST2",0,"8AC131.60-1");
			insertACOPOS("8V128M.001-2","SL1.IF1.ST3",0,"8AC131.60-1");
			insertNCNode("SPax[0]",2,"SPInit","8LSC75R15",0, "Powerlink", "SL1.IF1");
			insertNCNode("SPax[1]",3,"SPInit","8LSC75R15",0, "Powerlink", "SL1.IF1");
		default:
			break;
	}
	if(MA_FIX.ARCONF.UseAccMotor)
	{
		insertIOCard("8I84xxxxxxx.01P-1", "SL1.IF1.ST5", "8I84xxxxxxx.01P-1",1);
		insertNCNode("gAxis02",5,"gAxis02i","gAxis02a",0, "SDC", "SDC_IF1");
	}

 }

/********************************************************************
 * Static Functions.												*
 ********************************************************************/
static void memoryFree(void);

static int readDataObj(const char name[]) {
	memset(&datObj_Info,0,sizeof(DatObjInfo));
	datObj_Info.enable = 1;
	datObj_Info.pName = (UDINT)name;
			
	DatObjInfo(&datObj_Info);
	
	return datObj_Info.status;
}

static void autoSearch(void)
{
	int i = gDefCardCnt, ci = 0;
	char dummy[20] = {0,};
	
	dynAR_CardReset(&MA_FIX.ARCONF);

	for (i; i < (sizeof(ModuleInformation_addCard) / sizeof(ModuleInformation_addCard[0])); i++)
	{
		strcpy(dummy, ModuleInformation_addCard[i]);
		
		if (!strcmp(dummy, "X20AT6402"))
		{
			MA_FIX.ARCONF.ARCard[ci++].cardType = 1;
			MA_FIX.ARCONF.SnOptionModOK++;
		}
		
		else if (!strcmp(dummy, "X20AT2402"))
		{
			MA_FIX.ARCONF.ARCard[ci++].cardType = 2;
			MA_FIX.ARCONF.SnOptionModOK++;
		}
		
		else if (!strcmp(dummy, "X20DI9371"))
		{
			MA_FIX.ARCONF.ARCard[ci++].cardType = 3;
			MA_FIX.ARCONF.SnOptionModOK++;
		}
		
		else if (!strcmp(dummy, "X20DO9322"))
		{
			MA_FIX.ARCONF.ARCard[ci++].cardType = 4;
			MA_FIX.ARCONF.SnOptionModOK++;
		}
		
		else if (!strcmp(dummy, "X20DO8332"))
		{
			MA_FIX.ARCONF.ARCard[ci++].cardType = 5;
			MA_FIX.ARCONF.SnOptionModOK++;
		}
		
		else if (!strcmp(dummy, "X20AT4222"))
		{
			MA_FIX.ARCONF.ARCard[ci++].cardType = 6;
			MA_FIX.ARCONF.SnOptionModOK++;
		}
	}
	
	onDynPageRefresh = 1; // 20251024 HDE Add for error when change page
}

static int deleteDataObj(const char name[]) {
	static int status;
	
	if (status != ERR_FUB_BUSY) {
		readDataObj(name);
		
		if (datObj_Info.status == doERR_MODULNOTFOUND) {
			status = 0;
			return doERR_MODULNOTFOUND;
		}
	}
	
	datObj_Delete.enable	 = 1;
	datObj_Delete.ident		 = datObj_Info.ident;
	
	DatObjDelete(&datObj_Delete);
	status = datObj_Delete.status;

	return status;
}

static int createDataObj(const char name[], char *data) {
	
	datObj_Create.enable     = 1;
	datObj_Create.pName      = (unsigned long)name;
	datObj_Create.len        = strlen(data);
	datObj_Create.MemType    = doUSRROM;             /* Create in UserRAM */
	datObj_Create.Option     = 0;              /* disable Checksum */
	datObj_Create.pCpyData   = data;
			
	DatObjCreate(&datObj_Create);
	
	return datObj_Create.status;
}

static int moveDataObj(int ident) {

	datObj_Move.enable = 1;
	datObj_Move.ident = ident;
	datObj_Move.MemType = doUSRROM;
				
	DatObjMove(&datObj_Move);
	
	return datObj_Move.status;
}

static void memoryFree(void) {
	/* Global memory clear function. */
	if(DYN_DATA!=0){
		TMP_free(sizeof(dynData_typ),(void*)DYN_DATA);
		DYN_DATA = 0;
	}
}

/********************************************************************
 * Main Program Functions.											*
 ********************************************************************/

void _INIT ProgramInit(void)
{
	Dyn_Preset_Req = 0;
	dynAR_OptionCardSetting_Init(); //2021 주석갈김 TE 나중에 확인
	//initCardIndex();
}

void _CYCLIC ProgramCyclic(void)
{
	
	String_DynConfigCardDef(&MA_FIX.ARCONF);
	
	static int status;
	
	if (ACT_PIC == S_PIC_DYNCONFIG) {
		dynAR_OptionCardSetting_Load(); //2021 주석갈김 TE 나중에 확인
		
//		if (dyn_loadtrigger) {
//			dyn_loadtrigger = 0;
//			MA_FIX.ARCONF.SnOptionModOK = dynAR_OptionCardSetting(&MA_FIX.ARCONF_EXT);
//		}
		
//		if (Dyn_Option_Req) {
//			Dyn_Option_Req = 0;
//			dynAR_OptionCardSetting_Apply();
//		}
		dynAR_CardAdd(&MA_FIX.ARCONF);
		dynAR_CardRemove(&MA_FIX.ARCONF);
		
		if (Dyn_Preset_Req) {
			Dyn_Preset_Req = 0;		
			dynAR_Preset(&MA_FIX.ARCONF);
			dynAR_CardChange(&MA_FIX.ARCONF);
			
		}
//	
//		if (MA_FIX.ARCONF.ARReq) { //무엇? 
//			MA_FIX.ARCONF.ARReq = 0;
//			MA_FIX.ARCONF.ARPreset = 0;
//			
//			dynAR_CardChange(&MA_FIX.ARCONF);
//		}
	
		switch (DYN_AR.step) {
			case DYN_AR_STEP_IDLE:

				/* check request from external. */
				switch (DYN_AR.req) {
					case DYN_AR_REQ_ARCHG:
						DYN_AR.step = DYN_AR_STEP_PARSING;
						break;
					default:
						break;			
				}
				DYN_AR.req = 0;
		
				break;
			case DYN_AR_STEP_PARSING:
				/* Allocate memory for parsing data. */
				if (DYN_DATA==0) {
					TMP_alloc(sizeof(dynData_typ), (void*)&DYN_DATA);
					break;
				}
			
				/* Read Dynamic AR Config data. */
				status = readDataObj(DYN_AR_CONFIG_DATA);			
				if (status != ERR_OK) {
				DYN_AR.step = DYN_AR_STEP_ERROR;
				break;
				}
			
				/* Parse Dynamic AR Config data. */
				parseData(datObj_Info.pDatObjMem);
			
				DYN_AR.step = DYN_AR_STEP_NODE_MAKE;
			
				break;
			case DYN_AR_STEP_NODE_MAKE:
		
				/* Create Custom Node. */
				initArNodeData(arNode, DYN_AR_LEN_NODE, &arNodeNum);
				initNcNodeData(ncNode, DYN_AR_LEN_NODE, &ncNodeNum);
			
				createCustomNode();
				//DYN_AR.step = DYN_AR_STEP_IDLE;
				DYN_AR.step = DYN_AR_STEP_AR_MAKE;
			
				break;
			case DYN_AR_STEP_AR_MAKE:
				/* Allocate memory for parsing data. */
				if (arConfigBuf==0) {
					TMP_alloc(DYN_AR_LEN_ARCONFIG, (void*)&arConfigBuf);
					break;
				}
				memset(arConfigBuf,0,DYN_AR_LEN_ARCONFIG);
			
				/* Read Dynamic AR Config data. */
				//#ifndef D_SIMULATOR_MODE
				if(MA_FIX.ARCONF.MonitorVersion)
				{
					status = readDataObj(DYN_AR_AR_DEFAULT_PP580);
				}
				else
				{
					status = readDataObj(DYN_AR_AR_DEFAULT);
				}
				
				//#else
				//			status = readDataObj(DYN_AR_AR_SIM);
				//#endif			
				if (status != ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_ERROR;
					break;
				}
			
				/* Creatin AR Config. */
				createArconfig(arConfigBuf,datObj_Info.pDatObjMem, arNodeNum, arNode);
		
				DYN_AR.step = DYN_AR_STEP_AR_CLEAN;
			
				break;
			case DYN_AR_STEP_AR_CLEAN:
			
				status = deleteDataObj(DYN_AR_AR_TARGET);
			
				if (status == doERR_MODULNOTFOUND) {
					DYN_AR.step = DYN_AR_STEP_AR_SAVE;
				} else if (status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_AR_SAVE;
				} else if (status == ERR_FUB_BUSY) {
		
				} else {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
			
				break;
			case DYN_AR_STEP_AR_SAVE:		
			
				status = createDataObj(DYN_AR_AR_TARGET, arConfigBuf);
			
				if (status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_IO_MAKE;
				} else if (status == ERR_FUB_BUSY) {
		
				} else {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
			
				break;
			case DYN_AR_STEP_IO_MAKE:
			
				/* Allocate memory for parsing data. */
				if (ioMapBuf==0) {
					TMP_alloc(DYN_AR_LEN_IOCONFIG, (void*)&ioMapBuf);
					break;
				}
				memset(ioMapBuf,0,DYN_AR_LEN_IOCONFIG);
			
				/* Read Dynamic IO Config data. */
				status = readDataObj(DYN_AR_IO_DEFAULT);	
				if (status != ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_ERROR;
					break;
				}
			
				createIoConfig(ioMapBuf,datObj_Info.pDatObjMem, arNodeNum, arNode);
			
				DYN_AR.step = DYN_AR_STEP_IO_CLEAN;
		
				break;
			case DYN_AR_STEP_IO_CLEAN:

				status = deleteDataObj(DYN_AR_IO_TARGET);
			
				if (status == doERR_MODULNOTFOUND) {
					DYN_AR.step = DYN_AR_STEP_IO_SAVE;
				} else if (status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_IO_SAVE;
				} else if (status == ERR_FUB_BUSY) {
		
				} else {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
			
				break;
			case DYN_AR_STEP_IO_SAVE:
			
				status = createDataObj(DYN_AR_IO_TARGET, ioMapBuf);
			
				if (status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_AR_REMOVE;
				} else if (status == ERR_FUB_BUSY) {
	
				} else {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
		
				break;
			case DYN_AR_STEP_AR_REMOVE:
			
				arIO_Remove.enable = 1;
				arIO_Remove.pModuleName = (UDINT)DYN_AR_ARCONFIG;
			
				AsIOMMRemove(&arIO_Remove);
			
				if (arIO_Remove.status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_AR_CREATE;
				} else if (arIO_Remove.status > ERR_OK && arIO_Remove.status < ERR_FUB_ENABLE_FALSE) {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
		
				break;
			case DYN_AR_STEP_AR_CREATE:
			
				arIO_Create.enable = 1;
				arIO_Create.pDataObject = (UDINT)DYN_AR_AR_TARGET;
				arIO_Create.moduleKind = iommCONFIG;
				arIO_Create.pNewModule = (UDINT)DYN_AR_ARCONFIG;
				arIO_Create.memType = doUSRROM;
			
				AsIOMMCreate(&arIO_Create);
			
				if (arIO_Create.status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_IO_REMOVE;
				} else if (arIO_Create.status > ERR_OK && arIO_Create.status < ERR_FUB_ENABLE_FALSE) {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
			
				break;
			case DYN_AR_STEP_IO_REMOVE:
			
				arIO_Remove.enable = 1;
				arIO_Remove.pModuleName = (UDINT)DYN_AR_IOCONFIG;
			
				AsIOMMRemove(&arIO_Remove);
			
				if (arIO_Remove.status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_IO_CREATE;
				} else if (arIO_Remove.status > ERR_OK && arIO_Remove.status < ERR_FUB_ENABLE_FALSE) {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
			
				break;
			case DYN_AR_STEP_IO_CREATE:
			
				arIO_Create.enable = 1;
				arIO_Create.pDataObject = (UDINT)DYN_AR_IO_TARGET;
				arIO_Create.moduleKind = iommMAPPING;
				arIO_Create.pNewModule = (UDINT)DYN_AR_IOCONFIG;
				arIO_Create.memType = doUSRROM;
			
				AsIOMMCreate(&arIO_Create);
			
				if (arIO_Create.status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_NC_MAKE;
				} else if (arIO_Create.status > ERR_OK && arIO_Create.status < ERR_FUB_ENABLE_FALSE) {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
			
				break;
			case DYN_AR_STEP_NC_MAKE:
				/* Allocate memory for parsing data. */
				if (ncMapBuf==0) {
					TMP_alloc(DYN_AR_LEN_NCCONFIG, (void*)&ncMapBuf);
					break;
				}
				memset(ncMapBuf,0,DYN_AR_LEN_NCCONFIG);
			
				/* Read Dynamic NC Mapping data. */
				status = readDataObj(DYN_AR_NC_DEFAULT);			
				if (status != ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_ERROR;
					break;
				}
			
				/* Create NC Mapping. */			
				createNcMapping(ncMapBuf, datObj_Info.pDatObjMem, ncNodeNum, ncNode);
			
				DYN_AR.step = DYN_AR_STEP_NC_CLEAN;
			
				break;
			case DYN_AR_STEP_NC_CLEAN:
				status = deleteDataObj(DYN_AR_NC_TARGET);
			
				if (status == doERR_MODULNOTFOUND) {
					DYN_AR.step = DYN_AR_STEP_NC_SAVE;
				} else if (status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_NC_SAVE;
				} else if (status == ERR_FUB_BUSY) {
	
				} else {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
			
				break;
			case DYN_AR_STEP_NC_SAVE:
				status = createDataObj(DYN_AR_NC_TARGET, ncMapBuf);
			
				if (status == ERR_OK) {
					DYN_AR.step = DYN_AR_STEP_NC_CREATE;
				} else if (status == ERR_FUB_BUSY) {
	
				} else {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
	
				break;
			case DYN_AR_STEP_NC_CREATE:
			
				status = readDataObj(DYN_AR_NC_TARGET);
			
				if (status == ERR_OK) {
					memset(&ncmap_Create,0,sizeof(NCGLACT_NCDA_CREATE_typ));
					strcpy(ncmap_Create.parameter.name, DYN_AR_NC_TARGET);
					ncmap_Create.parameter.version	= 0x1000;
					ncmap_Create.parameter.type		= ncDATMOD_NC_MAPPING;
					ncmap_Create.parameter.nc_sw_id	= ncACP10MAN;
					ncmap_Create.parameter.data_len	= datObj_Info.len;
					ncmap_Create.parameter.data_adr	= datObj_Info.pDatObjMem;
				
					ncglobal_action(ncglobal_action_NCDA_CREATE,&ncmap_Create, sizeof(NCGLACT_NCDA_CREATE_typ));
			
					DYN_AR.step = DYN_AR_STEP_NC_MOVE;
			
				} else {
					DYN_AR.step = DYN_AR_STEP_ERROR;
				}
			
				break;
			case DYN_AR_STEP_NC_MOVE:
			
				if (ncmap_Create.status.ok) {
				
					status = moveDataObj(ncmap_Create.status.ident);
				
					if (status == ERR_OK) {
						DYN_AR.step = DYN_AR_STEP_DONE;
					} else if (status == ERR_FUB_BUSY ) {
			
					} else {
						DYN_AR.step = DYN_AR_STEP_ERROR;
					}
				
				}
				
				break;
			case DYN_AR_STEP_DONE:
				DYN_AR.step = DYN_AR_STEP_IDLE;
			
				memoryFree();
				SYSreset(1, 1);
			
				break;		
			case DYN_AR_STEP_ERROR:
				memoryFree();
			
				break;
			default:
				break;
		
		}
	}
}

