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

#define MAX_TEMPCARD 30

typedef enum {
    STEP_INIT = 0,
    STEP_WRITE,
    STEP_DONE
} SensorStep;

static BOOL GetTokenST(const char* src, char* out)
{
    if (src == 0 || strlen(src) == 0) return 0;

    char tmp[50];
    strcpy(tmp, src);

    char* t = strtok(tmp, ".");
    if (!t) return 0;
    t = strtok(0, ".");
    if (!t) return 0;
    t = strtok(0, ".");
    if (!t) return 0;

    strcpy(out, t);
    return 1;
}

static void GetChannelName(int index, const char* tokenST, char* out)
{
    if (tokenST && !strcmp(tokenST, "ST11"))
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

static UDINT GetSensorValue(USINT type)
{
    switch (type)
    {
        case 1: return 0x8000;
        case 2: return 0x8001;
        case 4: return 0x8002;
        case 3: return 0x8003;
        default: return 0x8000;
    }
}

static void SetSensorType(int index)
{
    char token[10] = {0};
    char chName[50] = {0};

    if (index >= MAX_TEMPCARD) return;
    if (!GetTokenST(ModuleInformation_tempcard[index], token)) return;

    GetChannelName(index, token, chName);

    USINT type = HT_FIX.Cardtype[index];
    UDINT val = GetSensorValue(type);

    AsIOAccWrite1.enable = 1;
    AsIOAccWrite1.pDeviceName = ModuleInformation_tempcard[index];
    AsIOAccWrite1.pChannelName = (unsigned long)chName;
    AsIOAccWrite1.value = val;
}

_CYCLIC void ht_org_cyclic(void)
{
    unsigned char zo;

    htCTRL(&ht_ctrl);

    for (zo = 0; zo < S_MAX_NO_ZONES; zo++)
    {
        zoneError[zo] = htGET(HEAT_GET_ZOERROR, zo);

        if (zoneError[zo] != 0 && HT_PAR.Zone[zo].Mode != 0)
        {
            if (zoneError[zo] & HEAT_ERR_SENSOR_BREAK)
                SET_HTZO_ALARM(zo, S_ALM_HTZO_SENSOR_BREAK)
            else
                RESET_HTZO_ALARM(zo, S_ALM_HTZO_SENSOR_BREAK)

            if (zoneError[zo] & HEAT_ERR_MAX_TEMP)
                SET_HTZO_ALARM(zo, S_ALM_HZZO_MAXTEMP)
            else
                RESET_HTZO_ALARM(zo, S_ALM_HZZO_MAXTEMP)
        }
    }

    if (HtTypeChangeReq == 100 || HtTypeChangeReq == 101)
    {
        switch (uiStepSetSensor)
        {
            case STEP_INIT:
                SetSensorType(uCounter);
                uiStepSetSensor = STEP_WRITE;
                break;

            case STEP_WRITE:
                AsIOAccWrite(&AsIOAccWrite1);
                if (AsIOAccWrite1.status != ERR_FUB_BUSY)
                    uiStepSetSensor = STEP_DONE;
                break;

            case STEP_DONE:
                if (AsIOAccWrite1.status == ERR_OK)
                {
                    uCounter++;
                    if (uCounter >= uTempCardCounter)
                    {
                        uCounter = 0;
                        HtTypeChangeReq = 0;
                    }
                }
                uiStepSetSensor = STEP_INIT;
                break;
        }
    }
}
