#pragma once
#include <cstdint>
#include <map>
#include <bitset>
#include <string>

using SignCode = std::bitset<32>;

struct EvState {
    // 摇杆|按键 释放|按下标识
    SignCode dsignCode;
    SignCode usignCode;
    SignCode lastdSignCode;
    SignCode lastuSignCode;

    mutable int8_t ttype = 0;
    mutable int8_t ltype = 0;
    mutable int macInd = -1;
    mutable int macIntev = -1;
    mutable int cTm = 0;
    mutable int lTm = 0;
    mutable int gctTm = 0;
    mutable bool isng = false;
    mutable bool redyMac = false;
    mutable bool debugFlag = false;
    // 按下计时
    mutable std::map<std::string, int> _ct;

    mutable unsigned long long curMac;

    // 摇杆|按键 按下数值
    std::map<std::string, int> dsbKvs;
    // 新按键
    std::vector<std::string> nsbKvs;
    // 摇杆|按键 释放数值
    std::map<std::string, int> usbKvs;
    std::map<std::string, std::string> btnVal;

    // 电量
    int Battery = 0;
    bool IsCharging = false;

    void fresh() const {
        if (!(ltype & 1) && (ttype & 1)) {
            isng = true;
            gctTm = 0;
            _ct.clear();
        }
        if (ttype != 0) {
            ltype = ttype;
            gctTm = 0;
        }

        cTm = GetTickCount();
        int t = cTm - lTm;
        gctTm += t;
        for (auto& c : _ct) {
            c.second += t;
        }
        for (auto& dsb : dsbKvs) {
            std::string name = "_ct." + dsb.first;
            if (_ct.find(name) == _ct.end()) {
                _ct[name] = 0;
            }
        }
    }
    void setDownType() {
        ttype |= 1;
        redyMac = true;
    }
    void setUpType() {
        ttype |= 2;
    }
    void sClear() {
        lastdSignCode = dsignCode;
        lastuSignCode = usignCode;
        dsignCode = 0;
        usignCode = 0;
        dsbKvs.clear();
        nsbKvs.clear();
        usbKvs.clear();
        ttype = 0;
        isng = false;
        if (cTm == 0) {
            cTm = GetTickCount();
        }
        lTm = cTm;
        btnVal.clear();
    }
    
};