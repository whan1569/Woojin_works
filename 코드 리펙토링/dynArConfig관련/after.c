#include <bur/plctypes.h>

#ifdef _DEFAULT_INCLUDES
#include <AsDefault.h>
#endif
/******************************************************************************
 * dynArConfig.c
 *
 * Refactoring History
 * - 2025-12-07 / LeeDonghwan
 *   - removed external STEP-based flow control
 *   - reorganized dynamic configuration flow into large functional units
 *   - split AR / IO / NC apply sequence into dedicated functions
 *   - cleaned up unnecessary comments and kept only high-level flow comments
 *   - normalized memory cleanup for dynamic buffers and temporary data
 *   - simplified node creation flow and fixed switch branch structure
 ******************************************************************************/

#include <string.h>
#include "dynArConfig.h"
#include "cardDefinition.h"

/********************************************************************
 * Global / retain
 ********************************************************************/

_GLOBAL_RETAIN ma_fix_typ MA_FIX;
_GLOBAL UINT ACT_PIC;

/********************************************************************
 * Local variables
 ********************************************************************/

_LOCAL int Dyn_Preset_Req;

_LOCAL AsIOMMRemove_typ arIO_Remove;
_LOCAL AsIOMMCreate_typ arIO_Create;
_LOCAL DatObjInfo_typ datObj_Info;
_LOCAL DatObjCreate_typ datObj_Create;
_LOCAL DatObjMove_typ datObj_Move;
_LOCAL DatObjDelete_typ datObj_Delete;
_LOCAL NCGLACT_NCDA_CREATE_typ ncmap_Create;

_LOCAL dynAR_typ DYN_AR;
_LOCAL dynData_typ *DYN_DATA;
_LOCAL char *arConfigBuf;
_LOCAL char *ioMapBuf;
_LOCAL char *ncMapBuf;

_LOCAL int arNodeNum;
_LOCAL dynArAdd_typ arNode[DYN_AR_LEN_NODE];

_LOCAL int ncNodeNum;
_LOCAL dynNcAdd_typ ncNode[DYN_AR_LEN_NODE];

/********************************************************************
 * Internal helpers
 ********************************************************************/

static int readDataObjNow(const char name[])
{
	memset(&datObj_Info, 0, sizeof(datObj_Info));

	datObj_Info.enable = 1;
	datObj_Info.pName  = (UDINT)name;

	DatObjInfo(&datObj_Info);

	return datObj_Info.status;
}

static int deleteDataObjNow(const char name[])
{
	int status;

	status = readDataObjNow(name);
	if (status == doERR_MODULNOTFOUND) {
		return doERR_MODULNOTFOUND;
	}
	if (status != ERR_OK) {
		return status;
	}

	memset(&datObj_Delete, 0, sizeof(datObj_Delete));
	datObj_Delete.enable = 1;
	datObj_Delete.ident  = datObj_Info.ident;

	DatObjDelete(&datObj_Delete);

	return datObj_Delete.status;
}

static int createDataObjNow(const char name[], char *data)
{
	memset(&datObj_Create, 0, sizeof(datObj_Create));

	datObj_Create.enable   = 1;
	datObj_Create.pName    = (UDINT)name;
	datObj_Create.len      = strlen(data);
	datObj_Create.MemType  = doUSRROM;
	datObj_Create.Option   = 0;
	datObj_Create.pCpyData = (UDINT)data;

	DatObjCreate(&datObj_Create);

	return datObj_Create.status;
}

static int moveDataObjNow(int ident)
{
	memset(&datObj_Move, 0, sizeof(datObj_Move));

	datObj_Move.enable  = 1;
	datObj_Move.ident   = ident;
	datObj_Move.MemType = doUSRROM;

	DatObjMove(&datObj_Move);

	return datObj_Move.status;
}

static void memoryFreeAll(void)
{
	if (DYN_DATA != 0) {
		TMP_free(sizeof(dynData_typ), (void*)DYN_DATA);
		DYN_DATA = 0;
	}

	if (arConfigBuf != 0) {
		TMP_free(DYN_AR_LEN_ARCONFIG, (void*)arConfigBuf);
		arConfigBuf = 0;
	}

	if (ioMapBuf != 0) {
		TMP_free(DYN_AR_LEN_IOCONFIG, (void*)ioMapBuf);
		ioMapBuf = 0;
	}

	if (ncMapBuf != 0) {
		TMP_free(DYN_AR_LEN_NCCONFIG, (void*)ncMapBuf);
		ncMapBuf = 0;
	}
}

/********************************************************************
 * Node build
 ********************************************************************/

static void createCustomNode(void)
{
	switch (MA_FIX.ARCONF.ARMap) {
		case 0:
			break;

		case 1:
			dynAR_Customize(&MA_FIX.ARCONF);
			dynAR_Customize_Record();
			break;

		default:
			break;
	}

	switch (MA_FIX.ARCONF.NCMap) {
		case DYN_AR_NC_P21_STD:
			insertACOPOS("8V128M.001-2", "SL1.IF1.ST2", 0, "8AC131.60-1");
			insertNCNode("SPax[0]", 2, "SPInit", "8LSC75R15", 0, "Powerlink", "SL1.IF1");
			break;

		case DYN_AR_NC_P21_M2:
			insertACOPOS("8V128M.001-2", "SL1.IF1.ST2", 0, "8AC131.60-1");
			insertACOPOS("8V128M.001-2", "SL1.IF1.ST3", 0, "8AC131.60-1");
			insertNCNode("SPax[0]", 2, "SPInit", "8LSC75R15", 0, "Powerlink", "SL1.IF1");
			insertNCNode("SPax[1]", 3, "SPInit", "8LSC75R15", 0, "Powerlink", "SL1.IF1");
			break;

		default:
			break;
	}

	if (MA_FIX.ARCONF.UseAccMotor) {
		insertIOCard("8I84xxxxxxx.01P-1", "SL1.IF1.ST5", "8I84xxxxxxx.01P-1", 1);
		insertNCNode("gAxis02", 5, "gAxis02i", "gAxis02a", 0, "SDC", "SDC_IF1");
	}
}

static void buildDynamicNodes(void)
{
	initArNodeData(arNode, DYN_AR_LEN_NODE, &arNodeNum);
	initNcNodeData(ncNode, DYN_AR_LEN_NODE, &ncNodeNum);

	createCustomNode();
}

/********************************************************************
 * Config apply
 ********************************************************************/

static int applyArConfig(void)
{
	int status;
	const char *defaultObj;

	if (arConfigBuf == 0) {
		TMP_alloc(DYN_AR_LEN_ARCONFIG, (void*)&arConfigBuf);
		if (arConfigBuf == 0) {
			return ERR_FUB_BUSY;
		}
	}

	memset(arConfigBuf, 0, DYN_AR_LEN_ARCONFIG);

	if (MA_FIX.ARCONF.MonitorVersion) {
		defaultObj = DYN_AR_AR_DEFAULT_PP580;
	} else {
		defaultObj = DYN_AR_AR_DEFAULT;
	}

	status = readDataObjNow(defaultObj);
	if (status != ERR_OK) {
		return status;
	}

	createArconfig(arConfigBuf, datObj_Info.pDatObjMem, arNodeNum, arNode);

	status = deleteDataObjNow(DYN_AR_AR_TARGET);
	if (!(status == ERR_OK || status == doERR_MODULNOTFOUND)) {
		return status;
	}

	status = createDataObjNow(DYN_AR_AR_TARGET, arConfigBuf);
	if (status != ERR_OK) {
		return status;
	}

	memset(&arIO_Remove, 0, sizeof(arIO_Remove));
	arIO_Remove.enable      = 1;
	arIO_Remove.pModuleName = (UDINT)DYN_AR_ARCONFIG;

	AsIOMMRemove(&arIO_Remove);
	if (!(arIO_Remove.status == ERR_OK || arIO_Remove.status == doERR_MODULNOTFOUND)) {
		return arIO_Remove.status;
	}

	memset(&arIO_Create, 0, sizeof(arIO_Create));
	arIO_Create.enable      = 1;
	arIO_Create.pDataObject = (UDINT)DYN_AR_AR_TARGET;
	arIO_Create.moduleKind  = iommCONFIG;
	arIO_Create.pNewModule  = (UDINT)DYN_AR_ARCONFIG;
	arIO_Create.memType     = doUSRROM;

	AsIOMMCreate(&arIO_Create);

	return arIO_Create.status;
}

static int applyIoConfig(void)
{
	int status;

	if (ioMapBuf == 0) {
		TMP_alloc(DYN_AR_LEN_IOCONFIG, (void*)&ioMapBuf);
		if (ioMapBuf == 0) {
			return ERR_FUB_BUSY;
		}
	}

	memset(ioMapBuf, 0, DYN_AR_LEN_IOCONFIG);

	status = readDataObjNow(DYN_AR_IO_DEFAULT);
	if (status != ERR_OK) {
		return status;
	}

	createIoConfig(ioMapBuf, datObj_Info.pDatObjMem, arNodeNum, arNode);

	status = deleteDataObjNow(DYN_AR_IO_TARGET);
	if (!(status == ERR_OK || status == doERR_MODULNOTFOUND)) {
		return status;
	}

	status = createDataObjNow(DYN_AR_IO_TARGET, ioMapBuf);
	if (status != ERR_OK) {
		return status;
	}

	memset(&arIO_Remove, 0, sizeof(arIO_Remove));
	arIO_Remove.enable      = 1;
	arIO_Remove.pModuleName = (UDINT)DYN_AR_IOCONFIG;

	AsIOMMRemove(&arIO_Remove);
	if (!(arIO_Remove.status == ERR_OK || arIO_Remove.status == doERR_MODULNOTFOUND)) {
		return arIO_Remove.status;
	}

	memset(&arIO_Create, 0, sizeof(arIO_Create));
	arIO_Create.enable      = 1;
	arIO_Create.pDataObject = (UDINT)DYN_AR_IO_TARGET;
	arIO_Create.moduleKind  = iommMAPPING;
	arIO_Create.pNewModule  = (UDINT)DYN_AR_IOCONFIG;
	arIO_Create.memType     = doUSRROM;

	AsIOMMCreate(&arIO_Create);

	return arIO_Create.status;
}

static int applyNcConfig(void)
{
	int status;

	if (ncMapBuf == 0) {
		TMP_alloc(DYN_AR_LEN_NCCONFIG, (void*)&ncMapBuf);
		if (ncMapBuf == 0) {
			return ERR_FUB_BUSY;
		}
	}

	memset(ncMapBuf, 0, DYN_AR_LEN_NCCONFIG);

	status = readDataObjNow(DYN_AR_NC_DEFAULT);
	if (status != ERR_OK) {
		return status;
	}

	createNcMapping(ncMapBuf, datObj_Info.pDatObjMem, ncNodeNum, ncNode);

	status = deleteDataObjNow(DYN_AR_NC_TARGET);
	if (!(status == ERR_OK || status == doERR_MODULNOTFOUND)) {
		return status;
	}

	status = createDataObjNow(DYN_AR_NC_TARGET, ncMapBuf);
	if (status != ERR_OK) {
		return status;
	}

	status = readDataObjNow(DYN_AR_NC_TARGET);
	if (status != ERR_OK) {
		return status;
	}

	memset(&ncmap_Create, 0, sizeof(ncmap_Create));
	strcpy(ncmap_Create.parameter.name, DYN_AR_NC_TARGET);
	ncmap_Create.parameter.version  = 0x1000;
	ncmap_Create.parameter.type     = ncDATMOD_NC_MAPPING;
	ncmap_Create.parameter.nc_sw_id = ncACP10MAN;
	ncmap_Create.parameter.data_len = datObj_Info.len;
	ncmap_Create.parameter.data_adr = datObj_Info.pDatObjMem;

	ncglobal_action(ncglobal_action_NCDA_CREATE, &ncmap_Create, sizeof(ncmap_Create));

	if (!ncmap_Create.status.ok) {
		return ERR_FUB_BUSY;
	}

	return moveDataObjNow(ncmap_Create.status.ident);
}

/********************************************************************
 * Main apply sequence
 ********************************************************************/

static int applyDynamicConfig(void)
{
	int status;

	if (DYN_DATA == 0) {
		TMP_alloc(sizeof(dynData_typ), (void*)&DYN_DATA);
		if (DYN_DATA == 0) {
			return ERR_FUB_BUSY;
		}
	}

	status = readDataObjNow(DYN_AR_CONFIG_DATA);
	if (status != ERR_OK) {
		return status;
	}

	parseData(datObj_Info.pDatObjMem);

	buildDynamicNodes();

	status = applyArConfig();
	if (status != ERR_OK) {
		return status;
	}

	status = applyIoConfig();
	if (status != ERR_OK) {
		return status;
	}

	status = applyNcConfig();
	if (status != ERR_OK) {
		return status;
	}

	return ERR_OK;
}

/********************************************************************
 * Program
 ********************************************************************/

void _INIT ProgramInit(void)
{
	Dyn_Preset_Req = 0;
	dynAR_OptionCardSetting_Init();
}

void _CYCLIC ProgramCyclic(void)
{
	int status;

	String_DynConfigCardDef(&MA_FIX.ARCONF);

	if (ACT_PIC != S_PIC_DYNCONFIG) {
		return;
	}

	dynAR_OptionCardSetting_Load();
	dynAR_CardAdd(&MA_FIX.ARCONF);
	dynAR_CardRemove(&MA_FIX.ARCONF);

	if (Dyn_Preset_Req) {
		Dyn_Preset_Req = 0;
		dynAR_Preset(&MA_FIX.ARCONF);
		dynAR_CardChange(&MA_FIX.ARCONF);
	}

	if (DYN_AR.req == DYN_AR_REQ_ARCHG) {
		DYN_AR.req = 0;

		status = applyDynamicConfig();

		if (status == ERR_OK) {
			memoryFreeAll();
			SYSreset(1, 1);
		} else {
			DYN_AR.error = status;
			memoryFreeAll();
		}
	}
}
