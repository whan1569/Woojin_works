#ifndef COREPULL_CONTROLLER_H
#define COREPULL_CONTROLLER_H

/* Portable C90 logic. All boolean fields use 0/1. */
#define CP_MODE_NONE       0
#define CP_MODE_MANUAL     1
#define CP_MODE_SETTING    2
#define CP_MODE_AUTO       3
#define CP_MODE_SEMI_AUTO  4

#define CP_IDLE            0
#define CP_READY           1
#define CP_STARTING        2
#define CP_RUNNING         3
#define CP_STOPPING        4
#define CP_COMPLETE        5

#define CP_CMD_NONE        0
#define CP_CMD_START       1
#define CP_CMD_STOP        2

#define CP_FB_NONE         0
#define CP_FB_ACCEPTED     1
#define CP_FB_RUNNING      2
#define CP_FB_DONE         3
#define CP_FB_ABORTED      4
#define CP_FB_REJECTED     5

#define CP_RESULT_NONE       0
#define CP_RESULT_OK         1
#define CP_RESULT_CANCEL     2
#define CP_RESULT_INTERLOCK  3
#define CP_RESULT_CONFIG     4
#define CP_RESULT_BUSY       5
#define CP_RESULT_TIMEOUT    6
#define CP_RESULT_AXIS       7
#define CP_RESULT_SEQUENCE   8

typedef struct {
    unsigned short time10ms;
    short speed;
    short pressure;
} corepull_params_typ;

/* Input snapshot: supplied by an audited machine-specific adapter.
 * permit: all applicable machine/axis safety and process permissions.
 * adapterReady: initialized, healthy adapter; remains true during own transaction.
 * On startup it must reconcile previous transactions and discard stale feedback.
 * autoRequest: maintained main-injection authorization, one per cycle;
 *              asserted before actual injection output and cleared after cycle.
 * injectionOn: actual main injection output, not pre-nozzle injection.
 * stopped: no pending starts, both directions inactive, outputs released.
 * Terminal feedback must only be published after stopped is confirmed.
 */
typedef struct {
    unsigned char enabled;
    unsigned char mode;
    unsigned char adapterReady;
    unsigned char permit;
    unsigned char cancel;
    unsigned char resetFault; /* deliberate reset, never ordinary cycle reset */
    unsigned char manualRequest;
    unsigned char autoRequest;
    unsigned char clampClosed;
    unsigned char nozzleTouched;
    unsigned char injectionOn;
    unsigned char axisAvailable;
    unsigned char paramsValid;
    unsigned char stopped;
    unsigned char feedback;
    unsigned long feedbackId;
    unsigned long elapsedMs;
    unsigned long startTimeoutMs;
    unsigned long runTimeoutMs;
    corepull_params_typ params;
} corepull_input_typ;

typedef struct {
    unsigned char state;
    unsigned char command;
    unsigned char result;
    unsigned char busy;
    unsigned char inhibitNextMotion; /* incompatible motion only, e.g. mold open */
    unsigned char attention;
    unsigned char mode;
    unsigned long requestId;
    corepull_params_typ params;
} corepull_output_typ;

typedef struct {
    unsigned char state;
    unsigned char result;
    unsigned char mode;
    unsigned char rearmed;
    unsigned char injectionLowSeen;
    unsigned char acceptedSeen;
    unsigned long requestId;
    unsigned long phaseMs;
    unsigned long startTimeoutMs;
    unsigned long runTimeoutMs;
    corepull_params_typ params;
} corepull_context_typ;

void CorePull_Init(corepull_context_typ *ctx);
void CorePull_Update(corepull_context_typ *ctx,
                     const corepull_input_typ *in,
                     corepull_output_typ *out);

/* Read-only machine observations. Not a safety release or ownership grant. */
typedef struct {
    unsigned char mode;
    unsigned char basicPermit;
    unsigned char clampClosed;
    unsigned char nozzleTouched;
    unsigned char injectionRaw;
    unsigned char axisQuiet;
    unsigned char paramsWithinLimits;
    unsigned char testRequest;
    unsigned char autoCandidate;
    unsigned char autoCandidateRise;
} corepull_observation_typ;

/* Only the existing axis/sequence owner may supply these fields.
 * Keep ready=0 until command consumption, cancellation, recipe exclusion,
 * next-motion inhibition and startup reconciliation are implemented.
 * Producer/controller/consumer must execute in deterministic order in the
 * same task, or provide a verified coherent transaction transport.
 * Never force these fields in Online Watch to start machinery.
 */
typedef struct {
    unsigned char ready;
    unsigned char permit;
    unsigned char cancel;
    unsigned char resetFault;
    unsigned char manualAuthorized;
    unsigned char autoRequest;
    unsigned char mainInjectionOn;
    unsigned char axisAvailable;
    unsigned char paramsValid;
    unsigned char stopped;
    unsigned char feedback;
    unsigned long feedbackId;
    unsigned long elapsedMs;
    unsigned long startTimeoutMs;
    unsigned long runTimeoutMs;
} corepull_owner_typ;

void CorePull_BuildInput(corepull_input_typ *in,
                         const corepull_observation_typ *obs,
                         const corepull_owner_typ *owner,
                         unsigned char enabled,
                         const corepull_params_typ *params);

/* Read-only preflight. These flags are observations, not safety permits. */
#define CP_BLOCK_OWNER       0x0001UL
#define CP_BLOCK_DISABLED    0x0002UL
#define CP_BLOCK_MODE        0x0004UL
#define CP_BLOCK_PERMIT      0x0008UL
#define CP_BLOCK_PARAMS      0x0010UL
#define CP_BLOCK_AXIS        0x0020UL
#define CP_BLOCK_SCANNER     0x0040UL
#define CP_BLOCK_COPY        0x0080UL
#define CP_BLOCK_JOG         0x0100UL
#define CP_BLOCK_CLAMP       0x0200UL
#define CP_BLOCK_NOZZLE      0x0400UL
#define CP_BLOCK_RECIPE      0x0800UL
#define CP_BLOCK_PUMP        0x1000UL
#define CP_BLOCK_PRENOZZLE   0x2000UL
#define CP_BLOCK_PARALLEL    0x4000UL

typedef struct {
    unsigned long manualBlockMask;
    unsigned long autoBlockMask;
    unsigned long lastTestPressMask;
    unsigned long lastInjectionRiseMask;
    unsigned long testPressCount;
    unsigned long testReleaseCount;
    unsigned long injectionRiseCount;
    unsigned long qualifiedInjectionCount;
    unsigned char co1TableMask; /* CUPos, CUNeg, IPNeg, Cool, EjAdv, EjRet */
    unsigned char samePump;
    unsigned char preNozzleOption;
    unsigned char parallelCore;
    unsigned char copyDone;
    unsigned char copyLocked;
    unsigned char manualIdle;
    unsigned char autoActive;
    unsigned char jogActive;
    unsigned char motorOn;
    unsigned char safetyModule;
    unsigned char previewOnly;
} corepull_preflight_typ;

/* Independent preflight never grants ownership. */
unsigned long CorePull_PreflightMask(const corepull_observation_typ *obs,
                                    const corepull_preflight_typ *p,
                                    unsigned char enabled,
                                    unsigned char automatic);

#endif
