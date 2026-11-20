/* Core Pull: two-file observation / preflight build (no physical actuation).
 * No direct axis, valve, pump or safety writes. An existing owner must
 * consume the transaction before live operation can be enabled.
 * Build on host with -DCOREPULL_HOST_TEST to omit Automation Studio task.
 */
#ifndef COREPULL_HOST_TEST
#ifdef _DEFAULT_INCLUDES
#include <AsDefault.h>
#endif
#include <bur/plc.h>
#include "defines.h"
#include "lib_types.h"
#include "mmi.h"
#include "mc.h"
#include "ma.h"
#include "io_vars.h"
#include "aut_def.h"
#include <AutSteps.h>
#include "lib_defs.h"
#endif
#include <string.h>
#include "corepull_controller.h"

static unsigned char IsAuto(unsigned char mode)
{
    return mode == CP_MODE_AUTO || mode == CP_MODE_SEMI_AUTO;
}

static unsigned char IsManual(unsigned char mode)
{
    return mode == CP_MODE_MANUAL || mode == CP_MODE_SETTING;
}

static void Enter(corepull_context_typ *ctx, unsigned char state)
{
    ctx->state = state;
    ctx->phaseMs = 0;
}

static void Finish(corepull_context_typ *ctx, unsigned char result)
{
    ctx->result = result;
    Enter(ctx, CP_COMPLETE);
}

static void Stop(corepull_context_typ *ctx, unsigned char result)
{
    ctx->result = result;
    Enter(ctx, CP_STOPPING);
}

static void CountTime(corepull_context_typ *ctx, unsigned long dt)
{
    /* Saturate rather than roll over; PLC unsigned long is 32 bits. */
    if (dt > 0xFFFFFFFFUL - ctx->phaseMs)
        ctx->phaseMs = 0xFFFFFFFFUL;
    else
        ctx->phaseMs += dt;
}

static unsigned char CancelReason(const corepull_context_typ *ctx,
                                 const corepull_input_typ *in)
{
    if (!in->enabled || in->cancel || in->mode != ctx->mode)
        return CP_RESULT_CANCEL;
    if (!in->adapterReady || !in->permit)
        return CP_RESULT_INTERLOCK;
    if (IsAuto(ctx->mode)) {
        if (!in->autoRequest || in->manualRequest)
            return CP_RESULT_CANCEL;
        if (!in->clampClosed)
            return CP_RESULT_INTERLOCK;
        if (ctx->state != CP_READY && !in->nozzleTouched)
            return CP_RESULT_INTERLOCK;
    } else {
        if (!in->manualRequest || in->autoRequest)
            return CP_RESULT_CANCEL;
    }
    return CP_RESULT_NONE;
}

static unsigned char ConfigOK(const corepull_input_typ *in)
{
    return in->paramsValid && in->params.time10ms > 0 &&
           in->params.speed > 0 && in->params.pressure > 0 &&
           in->startTimeoutMs > 0 &&
           in->runTimeoutMs > (unsigned long)in->params.time10ms * 10UL &&
           in->elapsedMs > 0;
}

static void Issue(corepull_context_typ *ctx, const corepull_input_typ *in)
{
    if (!in->axisAvailable || !in->stopped) {
        Finish(ctx, CP_RESULT_BUSY);
        return;
    }
    /* A new key for one and only one start/stop transaction. */
    if (ctx->requestId == 0xFFFFFFFFUL)
        ctx->requestId = 1;
    else
        ++ctx->requestId;
    ctx->acceptedSeen = 0;
    Enter(ctx, CP_STARTING);
}

void CorePull_Init(corepull_context_typ *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    /* Startup held requests cannot start a movement. */
    ctx->state = CP_IDLE;
}

void CorePull_Update(corepull_context_typ *ctx,
                     const corepull_input_typ *in,
                     corepull_output_typ *out)
{
    unsigned char reason;
    unsigned char fb;
    unsigned char terminal;
    unsigned char requestsLow;
    requestsLow = !in->manualRequest && !in->autoRequest;
    fb = (ctx->requestId != 0 && in->feedbackId == ctx->requestId)
         ? in->feedback : CP_FB_NONE;
    terminal = in->stopped && (fb == CP_FB_DONE ||
               fb == CP_FB_ABORTED || fb == CP_FB_REJECTED);

    switch (ctx->state) {
    case CP_IDLE:
        if (!in->adapterReady) {
            ctx->rearmed = 0;
            break;
        }
        if (requestsLow) {
            ctx->rearmed = in->stopped;
            break;
        }
        if (!ctx->rearmed) break;
        ctx->rearmed = 0;
        if (!in->enabled) break;
        ctx->mode = in->mode;
        if ((!IsManual(in->mode) && !IsAuto(in->mode)) ||
            (IsManual(in->mode) && (!in->manualRequest || in->autoRequest)) ||
            (IsAuto(in->mode) && (!in->autoRequest || in->manualRequest))) {
            Finish(ctx, CP_RESULT_SEQUENCE);
            break;
        }
        if (!ConfigOK(in)) {
            Finish(ctx, CP_RESULT_CONFIG);
            break;
        }
        ctx->params = in->params;
        ctx->startTimeoutMs = in->startTimeoutMs;
        ctx->runTimeoutMs = in->runTimeoutMs;
        ctx->result = CP_RESULT_NONE;
        ctx->injectionLowSeen = !in->injectionOn;
        Enter(ctx, CP_READY);
        reason = CancelReason(ctx, in);
        if (reason != CP_RESULT_NONE) {
            Finish(ctx, reason);
            break;
        }
        if (IsManual(ctx->mode)) Issue(ctx, in);
        /* Automatic must arm before the main injection output is high. */
        else if (in->injectionOn) Finish(ctx, CP_RESULT_SEQUENCE);
        break;

    case CP_READY:
        reason = CancelReason(ctx, in);
        if (reason != CP_RESULT_NONE) {
            Finish(ctx, reason);
            break;
        }
        if (IsAuto(ctx->mode)) {
            if (!in->injectionOn) ctx->injectionLowSeen = 1;
            if (in->injectionOn && ctx->injectionLowSeen) {
                if (!in->nozzleTouched) Finish(ctx, CP_RESULT_SEQUENCE);
                else Issue(ctx, in);
            }
        }
        break;

    case CP_STARTING:
        reason = CancelReason(ctx, in);
        if (reason != CP_RESULT_NONE) {
            Stop(ctx, reason);
            break;
        }
        if (terminal) {
            Finish(ctx, fb == CP_FB_DONE ? CP_RESULT_OK : CP_RESULT_AXIS);
            break;
        }
        if (fb == CP_FB_RUNNING) {
            ctx->acceptedSeen = 1;
            Enter(ctx, CP_RUNNING);
            break;
        }
        if (fb == CP_FB_ACCEPTED) ctx->acceptedSeen = 1;
        /* If acceptance vanishes without a terminal acknowledgement, cancel. */
        else if (ctx->acceptedSeen) {
            Stop(ctx, CP_RESULT_AXIS);
            break;
        }
        if (!in->elapsedMs) {
            Stop(ctx, CP_RESULT_CONFIG);
            break;
        }
        CountTime(ctx, in->elapsedMs);
        if (ctx->phaseMs >= ctx->startTimeoutMs)
            Stop(ctx, CP_RESULT_TIMEOUT);
        break;

    case CP_RUNNING:
        reason = CancelReason(ctx, in);
        if (reason != CP_RESULT_NONE) {
            Stop(ctx, reason);
            break;
        }
        if (terminal) {
            Finish(ctx, fb == CP_FB_DONE ? CP_RESULT_OK : CP_RESULT_AXIS);
            break;
        }
        /* An organizer falling back to waiting is not a new permission to run. */
        if (fb != CP_FB_RUNNING) {
            Stop(ctx, CP_RESULT_AXIS);
            break;
        }
        if (!in->elapsedMs) {
            Stop(ctx, CP_RESULT_CONFIG);
            break;
        }
        CountTime(ctx, in->elapsedMs);
        if (ctx->phaseMs >= ctx->runTimeoutMs)
            Stop(ctx, CP_RESULT_TIMEOUT);
        break;

    case CP_STOPPING:
        /* Do not clear STOP until the matching transaction is terminal and
         * there are no pending starts or active outputs. No timeout releases it. */
        if (terminal) Finish(ctx, ctx->result);
        break;

    case CP_COMPLETE:
        if (requestsLow && in->stopped && in->adapterReady &&
            (!IsAuto(ctx->mode) || ctx->result == CP_RESULT_OK ||
             in->resetFault)) {
            ctx->result = CP_RESULT_NONE;
            ctx->rearmed = 1;
            Enter(ctx, CP_IDLE);
        }
        break;

    default:
        /* Corrupted state must never start an axis. */
        if (ctx->requestId != 0) Stop(ctx, CP_RESULT_SEQUENCE);
        else Finish(ctx, CP_RESULT_SEQUENCE);
        break;
    }

    memset(out, 0, sizeof(*out));
    out->state = ctx->state;
    out->mode = ctx->mode;
    out->requestId = ctx->requestId;
    out->params = ctx->params;
    out->result = ctx->result;
    out->busy = ctx->state == CP_READY || ctx->state == CP_STARTING ||
                ctx->state == CP_RUNNING || ctx->state == CP_STOPPING;
    out->attention = ctx->result != CP_RESULT_NONE &&
                     ctx->result != CP_RESULT_OK &&
                     (ctx->result != CP_RESULT_CANCEL || IsAuto(ctx->mode));
    /* A failed auto transaction is not a release to advance the machine. */
    out->inhibitNextMotion = out->busy ||
        (ctx->state == CP_COMPLETE && IsAuto(ctx->mode) &&
         ctx->result != CP_RESULT_OK);
    if (ctx->state == CP_STARTING && !ctx->acceptedSeen)
        out->command = CP_CMD_START;
    else if (ctx->state == CP_STOPPING)
        out->command = CP_CMD_STOP;
}

/* Pure adapter: observations are necessary, not sufficient, for motion.
 * An owner must explicitly authorize both manual and automatic operation. */
void CorePull_BuildInput(corepull_input_typ *in,
                         const corepull_observation_typ *obs,
                         const corepull_owner_typ *owner,
                         unsigned char enabled,
                         const corepull_params_typ *params)
{
    memset(in, 0, sizeof(*in));
    in->enabled = enabled;
    in->mode = obs->mode;
    in->params = *params;
    in->adapterReady = owner->ready;
    in->permit = obs->basicPermit && owner->permit;
    in->cancel = owner->cancel;
    in->resetFault = owner->resetFault;
    /* Preserve the physical HMI request for release-to-rearm semantics;
     * authorization is a separate permit, not a synthesized button edge. */
    in->manualRequest = obs->testRequest;
    if (IsManual(obs->mode) && !owner->manualAuthorized)
        in->permit = 0;
    in->autoRequest = owner->autoRequest;
    in->clampClosed = obs->clampClosed;
    in->nozzleTouched = obs->nozzleTouched;
    in->injectionOn = owner->mainInjectionOn && obs->injectionRaw;
    in->axisAvailable = owner->axisAvailable && obs->axisQuiet;
    in->paramsValid = owner->paramsValid && obs->paramsWithinLimits;
    in->stopped = owner->stopped && obs->axisQuiet;
    in->feedback = owner->feedback;
    in->feedbackId = owner->feedbackId;
    in->elapsedMs = owner->elapsedMs;
    in->startTimeoutMs = owner->startTimeoutMs;
    in->runTimeoutMs = owner->runTimeoutMs;
}

/* Pure check: a zero observation mask apart from OWNER is still not permission. */
unsigned long CorePull_PreflightMask(const corepull_observation_typ *obs,
                                    const corepull_preflight_typ *p,
                                    unsigned char enabled,
                                    unsigned char automatic)
{
    unsigned long mask = CP_BLOCK_OWNER;
    if (!enabled) mask |= CP_BLOCK_DISABLED;
    if (!obs->basicPermit || !p->motorOn || !p->safetyModule)
        mask |= CP_BLOCK_PERMIT;
    if (!obs->paramsWithinLimits) mask |= CP_BLOCK_PARAMS;
    if (!obs->axisQuiet) mask |= CP_BLOCK_AXIS;
    if (!p->copyDone || (automatic ? !p->copyLocked : p->copyLocked))
        mask |= CP_BLOCK_COPY;
    if (automatic) {
        if (!IsAuto(obs->mode)) mask |= CP_BLOCK_MODE;
        if (!p->autoActive) mask |= CP_BLOCK_SCANNER;
        if (!obs->clampClosed) mask |= CP_BLOCK_CLAMP;
        if (!obs->nozzleTouched) mask |= CP_BLOCK_NOZZLE;
        if (p->co1TableMask) mask |= CP_BLOCK_RECIPE;
        if (p->samePump) mask |= CP_BLOCK_PUMP;
        if (p->preNozzleOption) mask |= CP_BLOCK_PRENOZZLE;
        if (p->parallelCore) mask |= CP_BLOCK_PARALLEL;
    } else {
        /* SETTING does not use the verified time-based movement path. */
        if (obs->mode != CP_MODE_MANUAL) mask |= CP_BLOCK_MODE;
        if (!p->manualIdle || p->autoActive) mask |= CP_BLOCK_SCANNER;
        if (p->jogActive) mask |= CP_BLOCK_JOG;
    }
    return mask;
}

#ifndef COREPULL_HOST_TEST
/* Match the other AS tasks' global declarations and retain layout. */
_GLOBAL mmi_typ MMI_DATA;
_GLOBAL ma_stat_typ MA_STAT;
_GLOBAL ax_dat_typ AXdat[MAX_NO_AXES];
_GLOBAL_RETAIN ea_fix_typ EA_FIX[S_MAX_NO_SIMPLE_AXES];
_GLOBAL au_par_typ AUT_PAR;
_GLOBAL io_typ gIO;
_GLOBAL BOOL gForceIO;
_GLOBAL BOOL DO_Remove_Runner_FWD;
_GLOBAL BOOL DO_Remove_Runner_BWD;
_GLOBAL tc_sta_typ TCmanual;
_GLOBAL tc_sta_typ TCauto;
_GLOBAL mo_dat_typ MOdat[S_MAX_NO_MOTOR];
_GLOBAL co_par_typ CO_PAR;
_GLOBAL ip_par_typ IP_PAR;
_GLOBAL_RETAIN ip_fix_typ IP_FIX;
_GLOBAL_RETAIN ma_fix_typ MA_FIX;

typedef struct {
    BOOL Enable;
    UINT ForwardTime;
    INT Speed;
    INT Pressure;
} corepull_config_typ;

_GLOBAL_RETAIN corepull_config_typ CorePullConfig;
_GLOBAL BOOL gCorePullTest;
_GLOBAL corepull_input_typ gCorePullInput;
_GLOBAL corepull_output_typ gCorePullOutput;
_GLOBAL corepull_observation_typ gCorePullObservation;
_GLOBAL corepull_owner_typ gCorePullOwner;
_LOCAL corepull_context_typ CorePullContext;
_GLOBAL corepull_preflight_typ gCorePullPreflight;
_LOCAL unsigned char PreviousInjectionRaw;
_LOCAL unsigned char PreviousTestRequest;
_LOCAL unsigned char FirstObservation;

static unsigned char ReadMode(void)
{
    switch (MMI_DATA.Mode) {
    case MODE_MANUAL: return CP_MODE_MANUAL;
    case MODE_SETTING: return CP_MODE_SETTING;
    case MODE_AUTO: return CP_MODE_AUTO;
    case MODE_SEMI_AUTO: return CP_MODE_SEMI_AUTO;
    default: return CP_MODE_NONE;
    }
}

static void ObserveMachine(corepull_observation_typ *obs,
                           const corepull_params_typ *params)
{
    const ax_dat_typ *ax = &AXdat[S_CO1];
    memset(obs, 0, sizeof(*obs));
    obs->mode = ReadMode();
    obs->testRequest = gCorePullTest != 0;
    obs->basicPermit = MA_STAT.AbSGClosed && MA_STAT.AbMaintenance &&
        !MA_STAT.AbEmStop && !gForceIO &&
        !gIO.DO.ClampCloseDifferencial &&
        !DO_Remove_Runner_FWD && !DO_Remove_Runner_BWD &&
        (AUT_PAR.SGCmode == S_GC_MODE_NONUSE);
    obs->clampClosed = AXdat[S_CU].Status == AX_STAT_NEG_END_POSITION;
    obs->nozzleTouched = AXdat[S_IU].Status == AX_STAT_NEG_END_POSITION;
    obs->injectionRaw = AXdat[S_IP].PhyOut.negDO1 != 0;
    obs->axisQuiet = !ax->POS.Org.Start && !ax->NEG.Org.Start &&
        !ax->POS.Con.Start && !ax->NEG.Con.Start &&
        !(ax->POS.Org.Status & CONTROL_STAT_ACTIVE) &&
        !(ax->NEG.Org.Status & CONTROL_STAT_ACTIVE) &&
        !(ax->POS.Con.Status & CONTROL_STAT_ACTIVE) &&
        !(ax->NEG.Con.Status & CONTROL_STAT_ACTIVE) &&
        !ax->PhyOut.posDO1 && !ax->PhyOut.negDO1 &&
        !ax->PmpReq.Req && !ax->PmpReq.Force && ax->SQ_act == 0 && ax->Sp_act == 0;
    obs->paramsWithinLimits = params->time10ms > 0 &&
        params->speed > 0 && params->pressure > 0 &&
        params->speed <= EA_FIX[S_CO1].POS.SvMaxVis &&
        params->pressure <= EA_FIX[S_CO1].POS.SpMaxVis;
    /* Candidate only: pre-nozzle/held outputs may appear here. Not an
     * authorization or a transaction; main-injection owner must qualify it. */
    obs->autoCandidate = CorePullConfig.Enable && IsAuto(obs->mode) &&
        obs->basicPermit && obs->clampClosed && obs->nozzleTouched &&
        obs->injectionRaw;
}

/* Read machine-owned data only. A changing recipe may produce a transient
 * snapshot; no write or authorization is derived from it. */
static void ObservePreflight(corepull_preflight_typ *p,
                             const corepull_observation_typ *obs)
{
    const co_mo_par_typ *tables[6];
    unsigned int table, row;
    p->previewOnly = 1;
    p->motorOn = MOdat[S_MOTOR_MAIN].AbOn != 0;
    p->safetyModule = MA_STAT.AbSafetyModuleState != 0;
    p->copyDone = MMI_DATA.DatCpy.Status == COPY_STAT_DONE;
    p->copyLocked = MMI_DATA.DatCpy.Locked != 0;
    /* sc_man.c defines S_STEP_IDLE as 0 (local to that task). */
    p->manualIdle = TCmanual.Step == 0 && !TCmanual.Stop &&
        (TCmanual.Status & CONTROL_STAT_ACTIVE) != 0;
    p->autoActive = TCauto.Start ||
        (TCauto.Status & CONTROL_STAT_ACTIVE) != 0;
    p->jogActive = MMI_DATA.Jog.ActPos != 0 || MMI_DATA.Jog.ActNeg != 0;
    p->samePump = IP_FIX.GEN.SPmpNumber == EA_FIX[S_CO1].GEN.SPmpNumber;
    p->preNozzleOption = IP_PAR.NEG.SbInjection_Bef_Nozzle_EN == 1 &&
        MA_FIX.OPT.SbInjection_Bef_NozzleFWD == 1;
    p->parallelCore = 0;
    p->co1TableMask = 0;
    tables[0] = CO_PAR.CUPos;
    tables[1] = CO_PAR.CUNeg;
    tables[2] = CO_PAR.IPNeg;
    tables[3] = CO_PAR.Cool;
    tables[4] = CO_PAR.EjAdv;
    tables[5] = CO_PAR.EjRet;
    for (table = 0; table < 6; ++table) {
        for (row = 0; row < MAX_NO_CORE_MOVES; ++row) {
            if (tables[table][row].CoreNr == S_CO1 &&
                (tables[table][row].Scode != AUTST_COMOV_UNDEFINED ||
                 tables[table][row].SRep != 0))
                p->co1TableMask |= (unsigned char)(1U << table);
            if (table == 2 && tables[table][row].Scode == AUTST_COMOV_PARALLEL)
                p->parallelCore = 1;
        }
    }
    p->manualBlockMask = CorePull_PreflightMask(obs, p, CorePullConfig.Enable, 0);
    p->autoBlockMask = CorePull_PreflightMask(obs, p, CorePullConfig.Enable, 1);
}

static void ObserveEdges(corepull_preflight_typ *p,
                         corepull_observation_typ *obs)
{
    unsigned char rise;
    obs->autoCandidateRise = 0;
    if (!FirstObservation) {
        if (obs->testRequest && !PreviousTestRequest) {
            ++p->testPressCount;
            p->lastTestPressMask = p->manualBlockMask;
        }
        if (!obs->testRequest && PreviousTestRequest)
            ++p->testReleaseCount;
        rise = obs->injectionRaw && !PreviousInjectionRaw;
        if (rise) {
            ++p->injectionRiseCount;
            p->lastInjectionRiseMask = p->autoBlockMask;
            /* Raw rising edge, not rising permit/nozzle/mode. Candidate only. */
            if (obs->autoCandidate &&
                (p->autoBlockMask & ~CP_BLOCK_OWNER) == 0) {
                obs->autoCandidateRise = 1;
                ++p->qualifiedInjectionCount;
            }
        }
    }
    PreviousInjectionRaw = obs->injectionRaw;
    PreviousTestRequest = obs->testRequest;
    FirstObservation = 0;
}

void _INIT CorePull_task_init(void)
{
    gCorePullTest = 0;
    memset(&gCorePullInput, 0, sizeof(gCorePullInput));
    memset(&gCorePullOutput, 0, sizeof(gCorePullOutput));
    memset(&gCorePullObservation, 0, sizeof(gCorePullObservation));
    memset(&gCorePullOwner, 0, sizeof(gCorePullOwner));
    memset(&gCorePullPreflight, 0, sizeof(gCorePullPreflight));
    gCorePullPreflight.previewOnly = 1;
    FirstObservation = 1;
    PreviousInjectionRaw = 0;
    PreviousTestRequest = 0;
    CorePull_Init(&CorePullContext);
}

void _CYCLIC CorePull_task_cyclic(void)
{
    corepull_params_typ params;
    params.time10ms = CorePullConfig.ForwardTime;
    params.speed = CorePullConfig.Speed;
    params.pressure = CorePullConfig.Pressure;

    ObserveMachine(&gCorePullObservation, &params);
    ObservePreflight(&gCorePullPreflight, &gCorePullObservation);
    ObserveEdges(&gCorePullPreflight, &gCorePullObservation);
    /* No owner integration is present in this build. Fail closed. */
    memset(&gCorePullOwner, 0, sizeof(gCorePullOwner));
    CorePull_BuildInput(&gCorePullInput, &gCorePullObservation,
                        &gCorePullOwner, CorePullConfig.Enable, &params);
    CorePull_Update(&CorePullContext, &gCorePullInput, &gCorePullOutput);
    /* No axis write here. Command must be consumed by an authorized owner. */
}
#endif
