// PE12
// 修改适配新版AvZ2，根据 https://github.com/qrmd0/AvZScript/blob/main/alterit/PE.%E7%BB%8F%E5%85%B8%E5%8D%81%E4%BA%8C%E7%82%AE/PE.%E7%BB%8F%E5%85%B812%E7%82%AE.cpp
#include <avz.h>

ATickRunner smart_blover;

bool IsZBExist(int type, int row, int abs, int hp) {
    for (auto & zb : aAliveZombieFilter) {
        if (zb.Type() == type && zb.Row() == row - 1 && zb.Abscissa() < abs && zb.Hp() >= hp && zb.State() != 70) {
            return true;
        }
    }
    return false;
}

bool IsZBBeforeExist() {
    for (auto & zb : aAliveZombieFilter) {
        if (zb.Abscissa() > 480) {
            return true;
        }
    }
    return false;
}

void SmartBlover() {
    for (int row : {1, 2, 3, 4, 5, 6}) {
        if (IsZBExist(ABALLOON_ZOMBIE, row, 0, 0)) {
            ACard(ABLOVER, 1, 8);
        }
    }
}

void DealDelay(int wave) {
    //通常波刷新延迟，用核弹
    AConnect(ATime(wave, 401), [wave] () {
        if (AGetMainObject()->RefreshCountdown() > 200 && AGetMainObject()->Wave() == wave) {
            AConnect(ANowTime(), [] () {
                for (auto & i : {ALILY_PAD, ADOOM_SHROOM, ACOFFEE_BEAN}) {
                    ACard(i, {{3, 9}, {4, 9}});
                }
            });
        }
    });

    //第10波刷新延迟，优先用樱桃，若仍延迟再用核
    if (wave == 10) {
        AConnect(ATime(wave, 1), [wave] () {
            int GGCount_up = 0;
            int GGCount_down = 0;
            for (auto & zb : aAliveZombieFilter) {
                if (zb.Type() == AGIGA_GARGANTUAR && (zb.Row() == 0 || zb.Row() == 1)) {
                    GGCount_up++;
                }
            }
            for (auto & zb : aAliveZombieFilter) {
                if (zb.Type() == AGIGA_GARGANTUAR && (zb.Row() == 4 || zb.Row() == 5)) {
                    GGCount_down++;
                }
            }
            auto zombie_type = AGetMainObject()->ZombieTypeList();
            if (zombie_type[AGIGA_GARGANTUAR] || zombie_type[ABUCKETHEAD_ZOMBIE]) {
                AConnect(ATime(wave, 399 - 100), [GGCount_down, GGCount_up] () {
                    ACard(ACHERRY_BOMB, GGCount_down < GGCount_up ? 2 : 5, 9);
                });
            }
        });
    }
}

//通常波341PP激活
void Normal_Wave(int wave) {
    AConnect(ATime(wave, 341 - 373), [] () {
        aCobManager.Fire({{2, 8.8}, {5, 8.8}});
    });
}

 //旗帜波
void Flag_Wave(int wave) {
    AConnect(ATime(wave, 341 - 373), [] () {
        aCobManager.Fire({{2, 8.8}, {5, 8.8}});
    });

    // w20冰消珊瑚
    if (wave == 20) {
        AConnect(ATime(wave, -300), [] () {
            ACard({{AICE_SHROOM, 1, 7}, {ACOFFEE_BEAN, 1, 7}});
        });
    }
}

//收尾
void Ending_Wave(int wave) {
    AConnect(ATime(wave, 341 - 373), [wave] () {
        for (int i = 0; i < 3; ++i) {
            AConnect(ANowDelayTime(601 * (i + 1)), [wave] () {
                if (AGetMainObject()->RefreshCountdown() > 200 && AGetMainObject()->Wave() == wave && IsZBBeforeExist()) {
                    AConnect(ANowTime(), [] () {
                        aCobManager.Fire({{2, 8.8}, {5, 8.8}});
                    });
                }
            });
        }
    });
}

void AScript() {
    ASetReloadMode(AReloadMode::MAIN_UI_OR_FIGHT_UI);

    ASelectCards({AICE_SHROOM, ACOFFEE_BEAN, ADOOM_SHROOM, ALILY_PAD,
        ACHERRY_BOMB, ABLOVER, AKERNEL_PULT, ACOB_CANNON, APEASHOOTER, ASUNFLOWER});

    // SetGameSpeed(5); // 以倍速运行
    //跳帧，已注释，如需要则去掉两端的 /* 以及 */ 符号
    /*SkipTick([=]() {
        return true;
    });*/

    smart_blover.Start(SmartBlover);

    for (int wave : {1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19}) {
        Normal_Wave(wave);
    }

    for (int wave : {10, 20}) {
        Flag_Wave(wave);
    }

    for (int wave : {9, 19, 20}) {
        Ending_Wave(wave);
    }

    for (int wave : {1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18}) {
        DealDelay(wave);
    }
}
