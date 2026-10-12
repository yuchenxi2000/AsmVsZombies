#ifndef __AVZ_MOD_LIFECYCLE_H__
#define __AVZ_MOD_LIFECYCLE_H__

#include <cstdint>

// Internal loader protocol. Script authors do not need to implement these states.
enum AModLifecycleState {
    AMOD_IDLE = 0,
    AMOD_SESSION = 1,
    AMOD_INITIALIZING = 2,
    AMOD_WAIT_MAIN = 4,
    AMOD_FINISHED = 8,
    AMOD_STOPPED = 16,
    AMOD_FAILED = 32,
};

struct __AModContext {
    uintptr_t board = 0;
    int ui = 0;
    int level = 0;
    int user = 0;
    int clock = 0;
};

// Keep the session transition rules independent of the game's calling conventions.
class __AModSession {
public:
    bool active = false;
    bool enteredFight = false;
    bool endPending = false;
    bool waitMain = false;
    bool finished = false;
    __AModContext origin;
    int lastClock = 0;

    void Begin(__AModContext context) {
        active = true;
        enteredFight = context.ui == 3;
        endPending = false;
        origin = context;
        lastClock = context.clock;
    }

    void Observe(__AModContext context) {
        if (!active)
            return;
        if (!context.board || (context.ui != 2 && context.ui != 3)
            || context.board != origin.board || context.level != origin.level
            || context.user != origin.user || (enteredFight && context.ui == 2)
            || (enteredFight && context.ui == 3 && context.clock < lastClock)) {
            endPending = true;
        } else if (context.ui == 3) {
            enteredFight = true;
            lastClock = context.clock;
        }
    }

    // reloadMode: NONE=0, MAIN_UI=1, MAIN_UI_OR_FIGHT_UI=2.
    void End(int reloadMode, __AModContext context) {
        active = false;
        endPending = false;
        enteredFight = false;
        finished = reloadMode == 0;
        waitMain = reloadMode == 1 && context.board != 0;
    }

    void ObserveMain(__AModContext context) {
        if (!context.board)
            waitMain = false;
    }

    bool CanBegin() const { return !active && !waitMain && !finished; }
};

#endif
