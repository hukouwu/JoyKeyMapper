#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <tuple>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <functional>

#include "ConnManage.h"
#include "Eval.h"
#include "KvInput.h"
#include "Util.h"

class KeyMapper {
public:
    struct RuleExpr {
        ExpressionEvaluator evaluator;
        SignCode dsignCode;
        SignCode usignCode;
        int nxSet = 2;
        int8_t flag = 0;// 时间变量|联动按键组回退
    };
    struct DKRecord {
        int ruleInd;
        int nodeSign;
        int nodeInd;
        bool del = false;
    };
    struct DNRecord {
        std::vector< DKRecord> downedKs;
    };

    struct DlUpKey {
        std::string keyNm;
        ExpressionEvaluator* upEval;
    };
    struct KeyExpr {
        int type = 0;
        std::string key;
        int outNum = 0;
        std::vector<int> propVars;
        ExpressionEvaluator evaluator;
        ExpressionEvaluator upEval;
    };
    struct KeyModNode {
        std::vector<KeyExpr>* keysP;
        std::vector<int> dInputNode;
        std::vector<int> uInputNode;
        std::vector<DlUpKey> duInputNode;
    };
    struct RuleMatch {
        std::vector<std::vector<KeyExpr>> keys;
        std::vector<RuleExpr> rules;
        std::vector<KeyModNode> keyModGroups;
        bool ctFlg = false;

        // 构造函数
        RuleMatch(std::vector<RuleExpr> rules, const std::string& expr, const std::vector<std::vector<KeyExpr>>& keys, const std::string& kes)
            : rules(rules), keys(keys) {
            for (std::vector<KeyExpr>& keyGroup : this->keys) {
                KeyModNode keyModNode;
                keyModNode.keysP = &keyGroup;
                for (int i = 0; i < keyGroup.size(); i++) {
                    KeyExpr& key = keyGroup[i];
                    if (key.type == 0) {
                        auto [type, outNum] = KvInput::tryDownInput(key.key);
                        key.type = type;
                        if (type != 0) {
                            if (key.outNum == 0) {
                                key.outNum = outNum;
                            }
                            if (type & 1) {
                                keyModNode.dInputNode.push_back(i);
                            }
                            if (type & 2) {
                                if (key.upEval.isEmpty) {
                                    keyModNode.uInputNode.push_back(i);
                                }
                                else {
                                    keyModNode.duInputNode.push_back({ key.key, &(key.upEval) });
                                }
                            }
                        }
                    }
                }

                std::reverse(keyModNode.uInputNode.begin(), keyModNode.uInputNode.end());
                keyModGroups.push_back(keyModNode);
            }
        }
    };

    // 加载配置文件
    bool loadConfig(const std::string& filename, ConnManage& connManage) {
        confFlag = false;
        std::ifstream file(filename);
        if (!file.is_open()) {
            SimpleLogger::LOG_INFO("[警告] 未找到配置文件: {}", filename);
            return false;
        }

        sm_ruleMatchs.clear();
        sm_downedNs.clear();
        ct_downedKs.clear();
        glbPms.clear();

        std::string line;
        int lineNum = 0;
        bool isSort = true;
        int ind = 0;
        std::vector<DKRecord> curDownedK;

        bool confFlg = false;
        std::map<std::string, std::map<std::string, std::string>> confs;
        std::string cnnname = connManage.getAliasNm();
        std::string currentBlockName = cnnname;
        while (std::getline(file, line)) {
            isSort = true;
            lineNum++;
            if (line.empty()) continue;

            // 配置开始
            if (!line.empty() && line[0] == '[') {
                confFlg = true;
                line = line.substr(1, line.size());
            }
            if (confFlg) {
                bool isEnd = false;
                // 配置结束
                if (!line.empty() && line[line.size() - 1] == ']') {
                    line = line.substr(0, line.size() - 1);
                    isEnd = true;
                }
                if (line.length() >= 6 &&
                    line.substr(0, 3) == "###" &&
                    line.substr(line.length() - 3) == "###") { // 新配置组
                    std::string comment = trim(line.substr(3, line.length() - 6));
                    currentBlockName = comment;
                    if (confs.find(currentBlockName) == confs.end()) {
                        confs[currentBlockName] = std::map<std::string, std::string>();
                    }
                }
                else if (!line.empty()) {
                    auto [key, value] = parseKeyAndValue(line);
                    confs[currentBlockName][key] = value;
                }
                if (isEnd) {
                    confFlg = false;
                }
                continue;
            }

            if (line[0] == '#') continue;
            SimpleLogger::LOG_INFO("解析: {}", line);
            // 短事件：按钮1+按钮2=按键1+按键2
            std::string splitChar = "→";
            size_t equalPos = line.find(splitChar);
            if (equalPos == std::string::npos) {
                SimpleLogger::LOG_INFO("格式错误1");
                continue;
            }

            std::string rulesExpStr = line.substr(0, equalPos);
            std::vector<RuleExpr> ruleGroups = getRuleExpre(rulesExpStr, connManage);
            if (ruleGroups.empty()) {
                SimpleLogger::LOG_INFO("rule表达式可能有误{}", rulesExpStr);
                continue;
            }
            std::string keysExpStr = line.substr(equalPos + splitChar.size());
            std::vector<std::vector<KeyExpr>> toKeys = getKeyPropExpre(keysExpStr);
            if (toKeys.empty()) {
                SimpleLogger::LOG_INFO("key表达式可能有误{}", keysExpStr);
                continue;
            }

            // 添加映射
            if (isSort) {
                sm_ruleMatchs.push_back(RuleMatch(ruleGroups, rulesExpStr, toKeys, keysExpStr));
                // 时间变量标识
                bool ctFlag = false;
                std::vector<RuleExpr>& tmpRules = sm_ruleMatchs.back().rules;
                for (int i = 0; i < tmpRules.size(); i++) {
                    RuleExpr& rule = tmpRules[i];
                    for (const std::string& varName : rule.evaluator.getUsedVariables()) {
                        if (varName.find("_ct.") != std::string::npos) {
                            rule.flag |= 2;
                            ctFlag = true;
                            break;
                        }
                    }
                }
                confFlag = true;
                curDownedK.push_back({ ind, 0, -1 });
                if (ctFlag) {
                    sm_ruleMatchs.back().ctFlg = true;
                }
                SimpleLogger::LOG_INFO("成功");
                ind++;
            }
        }
        sm_downedNs.push_back({ curDownedK });
        ct_downedKs.push_back(std::vector<int>());

        file.close();
        
        connManage.reloadConf(confs);
        return true;
    }

    // 短事件
    int matchSKeys(const EvState& state) {
        if (!confFlag) {
            return -1;
        }
        {
            std::vector<INPUT> inputs;
            for (auto it = delayUpKeys.begin(); it != delayUpKeys.end();){
                std::vector<double> results = it->upEval->eval({ &state.dsbKvs, &state.usbKvs, &state._ct, &glbPms });
                if (results.size() > 0 && results[0] > 0) {
                    SimpleLogger::LOG_INFO("延迟释放{}", it->keyNm);
                    auto [sign, input] = KvInput::parseUpInput(it->keyNm);
                    if (sign != 0) {
                        inputs.push_back(input);
                    }
                    it = delayUpKeys.erase(it);
                }
                else {
                    ++it;
                }
            }
            if (inputs.size() > 0) {
                SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
            }
        }
        
        if (state.ttype == 0) {
            if (state.macInd >= 0) {
                RuleMatch* match = &sm_ruleMatchs[state.macInd];
                SimpleLogger::LOG_DEBUG("延续规则{}", state.macInd);
                return state.macInd;
            }
        }
        else {
            if (state.macInd >= 0) {
                delSDownedKs(state);
                if (state.ttype == 2) {
                    state.redyMac = false;
                }
            }
        }
        if (!state.redyMac) {
            return -1;
        }
       
        bool isRf = false;
        DNRecord* curDownedN;
        DNRecord tmpDNRecord;
        int tmpDNRecordCnt = 0;
        if (!state.isng) {
            if (state.ttype & 2) {
                SimpleLogger::LOG_DEBUG("放");
                isRf = true;
            }
            else {
                SimpleLogger::LOG_DEBUG("按");
                sm_downedNs.back().downedKs.clear();
                ct_downedKs.back().clear();
            }
            curDownedN = &sm_downedNs.back();
        }
        else {
            if (state.ttype == 2) {
                SimpleLogger::LOG_DEBUG("首放");
                curDownedN = &tmpDNRecord;
                tmpDNRecordCnt++;
            }
            else {
                if (sm_downedNs.back().downedKs.size() <= 0) {
                    SimpleLogger::LOG_DEBUG("重按");
                    sm_downedNs.erase(sm_downedNs.begin() + 2, sm_downedNs.end());
                    sm_downedNs.back().downedKs.clear();
                    curDownedN = &sm_downedNs.back();
                    ct_downedKs.erase(ct_downedKs.begin() + 2, ct_downedKs.end());
                    ct_downedKs.back().clear();
                }
                else {
                    SimpleLogger::LOG_DEBUG("首按");
                    sm_downedNs.push_back({ std::vector<DKRecord>() });
                    curDownedN = &sm_downedNs.back();
                    ct_downedKs.push_back(std::vector<int>());
                }
            }
        }

    mclp:
        std::vector<DKRecord>* curDownedK = &curDownedN->downedKs;
        int lstNodePos = sm_downedNs.size() - 2 + tmpDNRecordCnt;
        std::vector<DKRecord>& lstDownedK = sm_downedNs[lstNodePos].downedKs;

        std::vector<int>* curctDK = &ct_downedKs.back();
        std::vector<int>& lstctDK = ct_downedKs[lstNodePos];
        lstctDK.clear();

        bool hasMatch = false;
        for (int i = 0; i < lstDownedK.size(); i++) {
            DKRecord& lstdkRecord = lstDownedK[i];
            if (lstdkRecord.del) {
                continue;
            }
            int cCatchedNodePos = lstdkRecord.nodeInd + 1;
            int ind = lstdkRecord.ruleInd;
            int madtchedNodeSign = lstdkRecord.nodeSign;
            RuleMatch* match = &sm_ruleMatchs[ind];
            bool isMatch = false;

            while (true) {// 向后查找无序联动组
                RuleExpr* rule = &(match->rules[cCatchedNodePos]);
                if ((madtchedNodeSign & (1 << cCatchedNodePos)) == 0) {
                    // 检查是否有超出规则外按下的键
                    if ((((rule->dsignCode ^ state.dsignCode) & state.dsignCode) == 0)
                        && (((rule->usignCode ^ state.usignCode) & state.usignCode) == 0)) {
                        std::vector<double> results = rule->evaluator.eval({ &state.dsbKvs, &state.usbKvs, &state._ct, &glbPms });
                        if (results[0] > 0) {
                            hasMatch = true;
                            isMatch = true;
                            madtchedNodeSign |= 1 << cCatchedNodePos;
                            curDownedK->push_back({ ind, madtchedNodeSign, cCatchedNodePos });
                        }
                    }
                }
                if (match->ctFlg) {
                    if (isMatch) {
                        curctDK->push_back(i);
                    }
                    else {
                        lstctDK.push_back(i);
                    }
                }

                if (!isMatch && (rule->nxSet < 2)) {
                    cCatchedNodePos += rule->nxSet;
                    if (cCatchedNodePos == lstdkRecord.nodeInd) {
                        break;
                    }
                    continue;
                }
                else {
                    break;
                }
            }

            if (!isMatch) {
                continue;
            }

            if (lstNodePos + 1 == match->rules.size()) {
                for (i++; i < lstDownedK.size(); i++) {
                    if (sm_ruleMatchs[lstDownedK[i].ruleInd].ctFlg) {
                        lstctDK.push_back(i);
                    }
                }
                state.macInd = ind;
                state.macIntev = -1;
                SimpleLogger::LOG_DEBUG("匹配规则{}", ind);
                return ind;
            }
            else {
                SimpleLogger::LOG_DEBUG("准规则{}", ind);
            }
        }

        if (hasMatch || isRf || lstNodePos < 1) {
            return -1;
        }

        if (state.dsignCode == 0) {
            delSDownedKs(state);
        }
        sm_downedNs.erase(sm_downedNs.begin() + 1, sm_downedNs.end());
        sm_downedNs.push_back({ std::vector<DKRecord>() });
        curDownedN = &sm_downedNs.back();
        ct_downedKs.erase(ct_downedKs.begin() + 1, ct_downedKs.end());
        ct_downedKs.push_back(std::vector<int>());
        goto mclp;
    }
    // 长按刷新规则
    bool freshSKeys(const EvState& state) {
        if (!confFlag) {
            return false;
        }
        std::vector<DKRecord>* downedK = &sm_downedNs.back().downedKs;
        if(state.macInd >= 0) {
            RuleExpr* rule = &(sm_ruleMatchs[state.macInd].rules.back());
            if (!(rule->flag & 2)) {
                return false;
            }
            else {
                std::vector<double> results = rule->evaluator.eval({ &state.dsbKvs, &state.usbKvs, &state._ct, &glbPms });
                if (results[0] > 0) {
                    return false;
                }
                else {
                    state.macInd = -1;
                }
            }
        }

        if (sm_downedNs.size() <= 1) {
            return false;
        }

        DNRecord& curDownedN = sm_downedNs.back();
        std::vector<DKRecord>& curDownedK = curDownedN.downedKs;
        std::vector<int>& curctDK = ct_downedKs.back();
        for (int i = 0; i < curctDK.size(); i++) {
            DKRecord* curdkRecord = &curDownedK[curctDK[i]];
            int ruleInd = curdkRecord->ruleInd;
            int nodeInd = curdkRecord->nodeInd;
            RuleExpr* rule = &(sm_ruleMatchs[ruleInd].rules[nodeInd]);
            std::vector<double> results = rule->evaluator.eval({ &state.dsbKvs, &state.usbKvs, &state._ct, &glbPms });
            if (results[0] <= 0) {
                curdkRecord->del = true;
            }
            else {
                curdkRecord->del = false;
            }
        }

        int matchedNodePos = sm_downedNs.size() - 2;
        std::vector<DKRecord>& lstDK = sm_downedNs[matchedNodePos].downedKs;
        std::vector<int>& lstctDK = ct_downedKs[matchedNodePos];
        bool isMatch = false;
        for (int i = 0; i < lstctDK.size(); i++) {
            DKRecord* lstdkRecord = &lstDK[lstctDK[i]];
            if (lstdkRecord->del) {
                continue;
            }
            int cCatchedNodePos = lstdkRecord->nodeInd + 1;
            int ind = lstdkRecord->ruleInd;
            int madtchedNodeSign = lstdkRecord->nodeSign;
            RuleMatch* match = &sm_ruleMatchs[ind];
            isMatch = false;

            while (true) {// 向后查找无序联动组
                RuleExpr* rule = &(match->rules[cCatchedNodePos]);
                if ((madtchedNodeSign & (1 << cCatchedNodePos)) == 0) {
                    // 检查是否有超出规则外按下的键
                    if ((((rule->dsignCode ^ state.dsignCode) & state.dsignCode) == 0)
                        && (((rule->usignCode ^ state.usignCode) & state.usignCode) == 0)) {
                        std::vector<double> results = rule->evaluator.eval({ &state.dsbKvs, &state.usbKvs, &state._ct, &glbPms });
                        if (results[0] > 0) {
                            isMatch = true;
                            madtchedNodeSign |= 1 << cCatchedNodePos;
                            curDownedK.push_back({ ind, madtchedNodeSign, cCatchedNodePos });
                        }
                    }
                }

                if (isMatch) {
                    curctDK.push_back(lstctDK[i]);
                    lstctDK.erase(lstctDK.begin() + i);
                    i--;
                }

                if (!isMatch && (rule->nxSet < 2)) {
                    cCatchedNodePos += rule->nxSet;
                    if (cCatchedNodePos == lstdkRecord->nodeInd) {
                        break;
                    }
                    continue;
                }
                else {
                    break;
                }
            }

            if (!isMatch) {
                continue;
            }

            if (matchedNodePos + 1 == match->rules.size()) {
                state.macInd = ind;
                state.macIntev = -1;
                return true;
            }
        }

        return false;
    }

    // 联动按键组回退
    void delSDownedKs(const EvState& state) {
        std::vector<DKRecord>* downedK = &sm_downedNs.back().downedKs;
        int lastInd = state.macInd;
        if (lastInd < 0) {
            sm_downedNs.erase(sm_downedNs.begin() + 1, sm_downedNs.end());
            ct_downedKs.erase(ct_downedKs.begin() + 1, ct_downedKs.end());
            return;
        }

        RuleMatch* match = &sm_ruleMatchs[lastInd];
        int nodePos = sm_downedNs.size() - 2;
        while (true) {
            sm_downedNs.pop_back();
            ct_downedKs.pop_back();
            nodePos--;
            if (nodePos < 0) {
                break;
            }
            if ((match->rules[nodePos].flag) & 1) {
                break;
            }
        }

        state.macInd = -1;
        state.isng = true;
    }

    struct KeyPack {
        int ruleIndex;
        std::vector<KeyModNode>* keyModGroups;
        std::map<std::string, int> ppmap;
        int gi = 0;
        int ni = 0;
        int delay = 0;
        int firstDelay = 1;
        int cnt = 0;
        ExpressionEvaluator* bkEval;
    };
    void loopKeyEvent(const EvState& state) {
        for (auto it = delayKeys.begin(); it != delayKeys.end(); ) {
            if (it->firstDelay > 0) {
                it->firstDelay -= state.dTm;
                if (it->firstDelay > 0) {
                    ++it;
                    continue;
                }
            }

            if (!(it->bkEval->isEmpty)) {
                std::vector<double> res = it->bkEval->eval({ &state.dsbKvs, &state.usbKvs, &state._ct, &glbPms });
                int typ = res[0];
                if (typ == 0) {
                    ++it;
                    continue;
                }
                else if (typ == 1) {
                }
                else if (typ == -1) {
                    it = delayKeys.erase(it);
                    continue;
                }
            }

            if (it->delay > 0) {
                it->delay -= state.dTm;
                if (it->delay > 0) {
                    ++it;
                    continue;
                }
            }
            int flg = excKeyNode(*it, state);
            if (flg < 0) {
                it = delayKeys.erase(it);
            }
            else {
                ++it;
            }
        }
    }
    void sendKeyEvent(int ruleInd, const EvState& state) {
        std::vector<KeyModNode>& keyModGroups = sm_ruleMatchs[ruleInd].keyModGroups;
        if (keyModGroups.empty()) return;

        std::map<std::string, int> mergedMap = state.dsbKvs;
        for (const auto& kv : state.usbKvs) {
            mergedMap[kv.first] = kv.second;
        }
        KeyPack kp = { ruleInd, &keyModGroups, mergedMap };
        int flg = excKeyNode(kp, state);
        if (flg >= 0) {
            delayKeys.push_back(kp);
        }
    }

    int excKeyNode(KeyPack& kp, const EvState& state) {
        std::vector<KeyModNode>& keyModGroups = *(kp.keyModGroups);
        int begni = kp.ni;
        for (int nodeInd = kp.gi; nodeInd < keyModGroups.size(); nodeInd++) {// 循环多组按键
            KeyModNode& kmn = keyModGroups[nodeInd];
            std::vector<KeyExpr>* keysP = kmn.keysP;

            int ind = begni;
            begni = 0;
            for (; ind < keysP->size(); ind++) {
                KeyExpr& key = (*keysP)[ind];
                switch (key.type) {
                case 5: {// _delay
                    std::vector<double> lpdts = key.evaluator.eval({ &kp.ppmap, &glbPms });
                    int delay = lpdts[0];
                    if (delay > 0) {
                        kp.delay = delay;
                        kp.gi = nodeInd;
                        kp.ni = ind;
                        return 1;
                    }
                    break;
                }
                case 6: {// $
                    std::vector<double> lpdts = key.evaluator.eval({ &kp.ppmap, &glbPms });
                    glbPms[key.key] = lpdts[0];
                    if (key.outNum > 0) {
                        state.macIntev = key.outNum;
                    }
                    else {
                        state.macIntev = -1;
                    }
                    break;
                }
                case 7: {// [
                    int limit = 0;
                    int delay = 0;
                    std::vector<double> lpdts = key.evaluator.eval({ &kp.ppmap, &glbPms });
                    if (lpdts.size() > 0) {
                        limit = lpdts[0];
                        if (lpdts.size() > 1) {
                            delay = lpdts[1];
                        }
                    }

                    int firstDelay = key.outNum;
                    int lpoi = key.propVars[0];
                    int lCnt = kp.cnt;

                    if (lCnt >= limit) {
                        nodeInd = lpoi;
                        break;
                    }

                    kp.cnt++;
                    if (lCnt == 0) {
                        break;
                    }
                    else if (lCnt == 1) {
                        kp.firstDelay = firstDelay;
                        if (delay > firstDelay) {
                            kp.firstDelay = delay;
                        }
                        kp.gi = nodeInd;
                        kp.ni = ind + 1;
                        kp.bkEval = &key.upEval;
                        return 1;
                    }
                    else {
                        kp.delay = delay;
                        return 1;
                    }
                    break;
                }
                case 8: {// 循环结束
                    nodeInd = key.propVars[0] - 1;
                    break;
                }
                default: break;
                }
            }

            // 按下键
            std::vector<INPUT> inputs;
            std::vector<int>& din = kmn.dInputNode;
            std::unordered_set<std::string> miss;
            for (int ind : din) {
                KeyExpr& key = (*keysP)[ind];
                std::vector<double> lpdts = key.evaluator.eval({ &kp.ppmap, &glbPms });
                auto [sign, input] = KvInput::parseDownInput(key.type, key.key, lpdts);
                if (sign != 0) {
                    SimpleLogger::LOG_DEBUG("发送按键{}", key.key);
                    inputs.push_back(input);
                    state.macIntev = key.outNum;
                }
                else {
                    miss.insert(key.key);
                }
            }
            UINT sentCount = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));

            inputs.clear();
            std::vector<int>& uin = kmn.uInputNode;
            // 释放键
            for (int ind : uin) {
                KeyExpr& key = (*keysP)[ind];
                if (miss.find(key.key) != miss.end()) {
                    continue;
                }
                auto [sign, input] = KvInput::parseUpInput(key.key);
                if (sign != 0) {
                    SimpleLogger::LOG_DEBUG("释放按键{}", key.key);
                    inputs.push_back(input);
                }
            }
            sentCount = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
            for (auto& duinput : kmn.duInputNode) {
                delayUpKeys.push_back( duinput );
            }

        }

        return -1;
    }

private:
    // 规则解析
    std::vector<RuleExpr> getRuleExpre(const std::string& rulesExpStr, ConnManage& connManage) {
        std::vector<RuleExpr> ruleGroups;

        int pos = 0;
        int beginPos = 0;
        int delCnt = 0;
        char c = 0;
        RuleExpr expr;
        while (pos < rulesExpStr.size()) {
            c = rulesExpStr[pos];
            
            if (c == '(') { // 找到(
                int cnt = 1;
                while (++pos < rulesExpStr.size() && cnt != 0) {
                    c = rulesExpStr[pos];
                    if (c == '(') {
                        cnt++;
                    }
                    else if (c == ')') {
                        cnt--;
                    }
                    if (pos >= rulesExpStr.size()) {
                        std::cerr << "[Warning] 缺少右括号" << std::endl;
                        return {};
                    }
                }
            }

            if (c == '~') {// 联动按键不要求顺序
                expr.nxSet = 0;
                if (!ruleGroups.empty() && ruleGroups.back().nxSet < 2) {
                    expr.nxSet = ruleGroups.back().nxSet - 1;
                    ruleGroups.back().nxSet = 1;
                }
                delCnt++;
            }

            if (c == ',' || c == ';') { // 另一组规则
                if (beginPos < pos - delCnt) {
                    std::string ruleExpStr = rulesExpStr.substr(beginPos, pos - delCnt - beginPos);
                    if (!expr.evaluator.setExpression(ruleExpStr)) {
                        return {};
                    }
                    connManage.indexSign(expr.evaluator.getUsedVariables(), expr.dsignCode, expr.usignCode);
                    if (c == ';') {
                        expr.flag |= 1;
                    }
                    ruleGroups.push_back(expr);

                    expr = RuleExpr();
                    delCnt = 0;
                }
                beginPos = pos + 1;
            }

            pos++;
        }
        if (beginPos < pos - delCnt) {
            std::string ruleExpStr = rulesExpStr.substr(beginPos, pos - delCnt - beginPos);
            if (!expr.evaluator.setExpression(ruleExpStr)) {
                return {};
            }
            connManage.indexSign(expr.evaluator.getUsedVariables(), expr.dsignCode, expr.usignCode);
            ruleGroups.push_back(expr);
        }
        return ruleGroups;
    }
    // 目标按键解析
    std::vector<std::vector<KeyExpr>> getKeyPropExpre(const std::string& keysExpStr) {
        std::list<std::list<KeyExpr>> toKeysGroups;
        toKeysGroups.push_back(std::list<KeyExpr>());
        std::list<KeyExpr>* toKeys;
        toKeys = &(toKeysGroups.back());
        toKeys->push_back(KeyExpr());
        KeyExpr* expr;
        expr = &(toKeys->back());

        std::stack<KeyExpr*> loopStk;
        int pos = 0;
        std::string name = "";
        std::string out = "";
        while (pos < keysExpStr.size()) {
            char c = keysExpStr[pos];
            
            if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9') || c == '_') {
                name += c;
            } else {
                if (!name.empty()) {
                    expr->key = name;
                    if (name == "_delay") {
                        expr->type = 5;
                    }
                    name.clear();
                }
                
                if (c == '(') { // 找到(
                    int cnt = 1;
                    int begPos = pos;
                    while (++pos < keysExpStr.size()) {
                        c = keysExpStr[pos];
                        if (c == '(') {
                            cnt++;
                        }
                        else if (c == ')') {
                            cnt--;
                            if (cnt == 0) {
                                std::string expStr = keysExpStr.substr(begPos + 1, pos - begPos - 1);
                                if (!expr->evaluator.setExpression(expStr)) {
                                    return {};
                                }
                                c = keysExpStr[++pos];
                                break;
                            }
                        }
                        if (pos >= keysExpStr.size()) {
                            std::cerr << "[Warning] 缺少右括号" << std::endl;
                            return {};
                        }
                    }
                }

                if (c == ':') { // 返回
                    int khFlag = 0;
                    int groupIndex = 0;
                    pos++;
                    while (pos < keysExpStr.size()) {
                        c = keysExpStr[pos];
                        
                        bool breakFlag = false;
                        switch (c) {
                            case ' ':
                                break;
                            case '(':
                                khFlag++;
                                break;
                            case ')':
                                khFlag--;
                                break;
                            case ',':
                                switch (groupIndex) {
                                case 0:
                                    if (!out.empty()) {
                                        expr->outNum = atoi(out.c_str());
                                        out.clear();
                                    }
                                    break;
                                case 1:
                                    break;
                                default:
                                    break;
                                }
                                groupIndex++;
                                break;
                            default:
                                if (khFlag > 0) {
                                    out += c;
                                }
                                else {
                                    breakFlag = true;
                                }
                                break;
                        }
                        if (breakFlag) {
                            break;
                        }
                        pos++;
                    }
                    if (!out.empty()) {
                        switch (groupIndex) {
                        case 0:
                            expr->outNum = atoi(out.c_str());
                            out.clear();
                            break;
                        case 1:
                            expr->upEval.setExpression(out);
                            break;
                        default:
                            break;
                        }
                    }
                }

                if (c == '$') {
                    expr->type = 6;
                    pos++;
                    continue;
                }
                
                if (c == '[') { // 循环开始
                    expr->type = 7;
                    expr->key = "[";
                    expr->propVars.push_back(toKeysGroups.size() - 1);
                    loopStk.push(expr);
                    c = ',';
                }

                if (!expr->key.empty() || !expr->evaluator.isEmpty) {
                    toKeys->push_back(KeyExpr());
                    expr = &(toKeys->back());
                }

                if (c == ']') { // 结束
                    if (toKeys->size() > 1) {
                        toKeys->pop_back();
                        toKeysGroups.push_back(std::list<KeyExpr>());
                        toKeys = &(toKeysGroups.back());
                        toKeys->push_back(KeyExpr());
                        expr = &(toKeys->back());
                    }

                    expr->type = 8;
                    expr->key = "]";
                    KeyExpr* bk = loopStk.top();
                    loopStk.pop();
                    expr->propVars.push_back(bk->propVars[0]);
                    bk->propVars[0] = toKeysGroups.size() - 1;
                }

                if (c == ',') { // 另一组按键
                    if (toKeys->size() > 1) {
                        toKeys->pop_back();
                        toKeysGroups.push_back(std::list<KeyExpr>());
                        toKeys = &(toKeysGroups.back());
                        toKeys->push_back(KeyExpr());
                        expr = &(toKeys->back());
                    }
                }
            }

            pos++;
        }
        if (!name.empty()) {
            expr->key = name;
        }
        if (expr->key.empty()) {
            toKeys->pop_back();
        }
        if (toKeys->empty()) {
            toKeysGroups.pop_back();
        }

        std::vector<std::vector<KeyExpr>> retKeysGroups;
        retKeysGroups.reserve(toKeysGroups.size());
        std::transform(toKeysGroups.begin(), toKeysGroups.end(),
            std::back_inserter(retKeysGroups),
            [](const std::list<KeyExpr>& lst) {
                return std::vector<KeyExpr>(lst.begin(), lst.end());
            });
        return retKeysGroups;
    }
public:
    // 短事件映射表：源按钮列表 -> 目标按键列表
    std::vector<RuleMatch> sm_ruleMatchs;
    std::vector<DNRecord> sm_downedNs;
    std::vector<std::vector<int>> ct_downedKs;
    std::map<std::string, int> glbPms;
    std::vector<DlUpKey> delayUpKeys;
    std::vector<KeyPack> delayKeys;
    bool confFlag = false;
};
