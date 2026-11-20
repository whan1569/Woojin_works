#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "corepull_controller.h"

static corepull_context_typ c;
static corepull_input_typ i;
static corepull_output_typ o;
static void tick(void) { CorePull_Update(&c, &i, &o); }
static void init(void)
{
    CorePull_Init(&c);
    memset(&i, 0, sizeof(i));
    i.mode = CP_MODE_AUTO;
    i.enabled = i.adapterReady = i.permit = i.clampClosed = 1;
    i.axisAvailable = i.stopped = i.paramsValid = 1;
    i.params.time10ms = 20;
    i.params.speed = 100;
    i.params.pressure = 200;
    i.elapsedMs = 20;
    i.startTimeoutMs = 60;
    i.runTimeoutMs = 400;
}
static void arm(void)
{
    init(); tick();
    i.autoRequest = 1; tick();
    assert(o.state == CP_READY && o.command == CP_CMD_NONE);
}
static void start(void)
{
    arm();
    i.nozzleTouched = 1; tick();
    assert(o.state == CP_READY);
    i.injectionOn = 1; tick();
    assert(o.state == CP_STARTING && o.command == CP_CMD_START);
}
static void feedback(unsigned char fb, unsigned char stopped)
{
    i.feedbackId = o.requestId; i.feedback = fb; i.stopped = stopped; tick();
}
static void run(void)
{
    start(); feedback(CP_FB_ACCEPTED, 0);
    assert(o.command == CP_CMD_NONE && o.state == CP_STARTING);
    feedback(CP_FB_RUNNING, 0);
    assert(o.state == CP_RUNNING);
}
int main(void)
{
    unsigned long id;
    init(); i.autoRequest = 1; tick();
    assert(o.state == CP_IDLE && o.command == CP_CMD_NONE);
    i.autoRequest = 0; tick(); i.autoRequest = 1; tick();
    assert(o.state == CP_READY);
    puts("PASS: startup held request cannot start");

    run(); id = o.requestId;
    i.injectionOn = 0; tick(); assert(o.state == CP_RUNNING);
    feedback(CP_FB_DONE, 1); assert(o.state == CP_COMPLETE && o.result == CP_RESULT_OK);
    tick(); assert(o.command == CP_CMD_NONE && o.requestId == id);
    i.autoRequest = 0; tick(); assert(o.state == CP_IDLE);
    i.autoRequest = 1; tick(); i.injectionOn = 1; tick();
    assert(o.command == CP_CMD_START && o.requestId != id);
    puts("PASS: auto start, injection latch, normal feedback, one start per request");

    run(); i.permit = 0; tick(); assert(o.command == CP_CMD_STOP);
    i.permit = 1; tick(); assert(o.command == CP_CMD_STOP);
    i.feedback = CP_FB_ABORTED; i.stopped = 0; tick(); assert(o.command == CP_CMD_STOP);
    i.stopped = 1; tick(); assert(o.state == CP_COMPLETE && o.inhibitNextMotion);
    i.autoRequest = 0; tick(); assert(o.state == CP_COMPLETE);
    i.resetFault = 1; tick(); assert(o.state == CP_IDLE);
    puts("PASS: interlock loss, no restart, stop confirmation, deliberate reset");

    start(); i.cancel = 1; tick(); assert(o.command == CP_CMD_STOP);
    i.feedback = CP_FB_ABORTED; i.feedbackId = o.requestId + 1; tick();
    assert(o.command == CP_CMD_STOP);
    feedback(CP_FB_ABORTED, 1); assert(o.state == CP_COMPLETE);
    puts("PASS: cancel before acceptance and stale acknowledgement rejected");

    start(); tick(); tick(); tick();
    assert(o.command == CP_CMD_STOP && o.result == CP_RESULT_TIMEOUT);
    puts("PASS: missing start acknowledgement times out");

    run(); feedback(CP_FB_ACCEPTED, 0);
    assert(o.command == CP_CMD_STOP && o.result == CP_RESULT_AXIS);
    puts("PASS: organizer restart/wait feedback requests stop");

    arm(); i.injectionOn = 1; tick();
    assert(o.state == CP_COMPLETE && o.result == CP_RESULT_SEQUENCE);
    init(); tick(); i.autoRequest = 1; i.injectionOn = 1; tick();
    assert(o.state == CP_COMPLETE && o.command == CP_CMD_NONE);
    puts("PASS: reject early or already-high injection output");

    init(); i.mode = CP_MODE_MANUAL; tick(); i.manualRequest = 1; tick();
    assert(o.command == CP_CMD_START);
    feedback(CP_FB_RUNNING, 0); i.manualRequest = 0; tick();
    assert(o.command == CP_CMD_STOP);
    feedback(CP_FB_ABORTED, 1); assert(o.state == CP_COMPLETE);
    tick(); assert(o.state == CP_IDLE);
    puts("PASS: manual button release requests stop");

    init(); tick(); i.params.time10ms = 0; i.autoRequest = 1; tick();
    assert(o.result == CP_RESULT_CONFIG && o.command == CP_CMD_NONE);
    arm(); i.axisAvailable = 0; i.nozzleTouched = i.injectionOn = 1; tick();
    assert(o.result == CP_RESULT_BUSY && o.command == CP_CMD_NONE);
    puts("PASS: invalid parameter and busy axis rejection");

    run(); i.elapsedMs = 400; tick();
    assert(o.command == CP_CMD_STOP && o.result == CP_RESULT_TIMEOUT);
    puts("PASS: running watchdog requests stop");
    return 0;
}
