#include <avz.h>

void AScript() {
    ASetReloadMode(AReloadMode::MAIN_UI_OR_FIGHT_UI);
    AConnect('L', []{
        static bool isPaused = false;
        isPaused = !isPaused;
        ASetAdvancedPause(isPaused);
    });
}
