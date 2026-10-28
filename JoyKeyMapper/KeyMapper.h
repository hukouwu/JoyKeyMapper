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
    struct KeyExpr {
        int type = 0;
        std::string key;
        int outNum = 0;
        ExpressionEvaluator evaluator;
        ExpressionEvaluator upEval;
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
    struct RuleMatch {
        std::vector<std::vector<KeyExpr>> keys;
        std::vector<RuleExpr> rules;
        bool ctFlg = false;

        // 构造函数
        RuleMatch(std::vector<RuleExpr> rules, const std::string& expr, const std::vector<std::vector<KeyExpr>>& keys, const std::string& kes)
            : rules(rules), keys(keys) {
        }
    };
    struct DlUpKey {
        std::string keyNm;
        ExpressionEvaluator upEval;
        INPUT input;
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
            bool confFlgTmp = confFlg;
            // 配置结束
            if (!line.empty() && line[line.size() - 1] == ']') {
                line = line.substr(0, line.size() - 1);
                confFlgTmp = false;
            }
            if (confFlg) {
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
                confFlg = confFlgTmp;
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
    std::tuple<std::vector<std::vector<KeyExpr>>, int> matchSKeys(const EvState& state) {
        if (!confFlag) {
            return { };
        }
        {
            std::vector<INPUT> inputs;
            for (auto it = delayUpKeys.begin(); it != delayUpKeys.end();){
                std::vector<double> results = it->upEval.eval({ state.dsbKvs, state.usbKvs, state._ct, glbPms });
                if (results.size() > 0 && results[0] > 0) {
                    SimpleLogger::LOG_INFO("延迟释放{}", it->keyNm);
                    inputs.push_back(it->input);
                    KvInput::setUp(it->keyNm);
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
                return std::make_tuple(match->keys, state.macInd);
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
            return {};
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
                        std::vector<double> results = rule->evaluator.eval({ state.dsbKvs, state.usbKvs, state._ct, glbPms });
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
                return std::make_tuple(match->keys, ind);
            }
            else {
                SimpleLogger::LOG_DEBUG("准规则{}", ind);
            }
        }

        if (hasMatch || isRf || lstNodePos < 1) {
            return {};
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
                std::vector<double> results = rule->evaluator.eval({ state.dsbKvs, state.usbKvs, state._ct, glbPms });
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
            std::vector<double> results = rule->evaluator.eval({ state.dsbKvs, state.usbKvs, state._ct, glbPms });
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
                        std::vector<double> results = rule->evaluator.eval({ state.dsbKvs, state.usbKvs, state._ct, glbPms });
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
    
    struct LpInfo {
        int begin = 0;
        int limit = 2;
        int delay = 0;
    };
    struct keyPropsOut {
        std::string name;
        std::vector<double> props;
        int out = 0;
        KeyExpr* keyExpr;
    };
    struct keyUpRec {
        std::string name;
        int index;
    };
    void sendKeyEvent(std::vector<std::vector<KeyExpr>>& sendKeys, const EvState& state) {
        if (sendKeys.empty()) return;

        std::vector<LpInfo> lpstack;
        LpInfo curLpInfo;
        for (int nodeInd = 0; nodeInd < sendKeys.size(); nodeInd++) {// 循环多组按键
            std::vector<KeyExpr>& keys = sendKeys[nodeInd];
            std::vector<keyPropsOut> nmKeyPropsOut; // 一组按键名称和传参
            bool skipSend = false;

            for (KeyExpr& key : keys) {
                if (key.type != 0) {
                    skipSend = true;
                    switch (key.type) {
                    case 1:  {
                        lpstack.push_back(curLpInfo);
                        curLpInfo = LpInfo();

                        curLpInfo.begin = nodeInd;
                        std::vector<double> lpdts = key.evaluator.eval({ state.dsbKvs, state.usbKvs, glbPms });
                        if (lpdts.size() > 0) {
                            curLpInfo.limit = lpdts[0];
                            if (lpdts.size() > 1) {
                                curLpInfo.delay = lpdts[1];
                            }
                        }

                        break;
                    }
                    case 2: {
                        curLpInfo.limit -= curLpInfo.limit > 0;
                        if (curLpInfo.limit != 0) {
                            nodeInd = curLpInfo.begin;
                            if (curLpInfo.delay > 0) {
                                Sleep(curLpInfo.delay);
                            }
                            break;
                        }
                        
                        curLpInfo = lpstack.back();
                        lpstack.pop_back();
                        
                        break;
                    }
                    case 3: {
                        std::vector<double> lpdts = key.evaluator.eval({ state.dsbKvs, state.usbKvs, glbPms });
                        int delay = lpdts[0];
                        if (delay > 0) {
                            Sleep(delay);
                        }
                        break;
                    }
                    case 4: {
                        std::vector<double> lpdts = key.evaluator.eval({ state.dsbKvs, state.usbKvs, glbPms });
                        glbPms[key.key] = lpdts[0];
                        SimpleLogger::LOG_DEBUG("自定义变量{}={}", key.key, lpdts[0]);
                        if (key.outNum > 0) {
                            state.macIntev = key.outNum;
                        }
                        else {
                            state.macIntev = -1;
                        }
                        break;
                    }
                    default: break;
                    }

                    if (skipSend) {
                        break;
                    }
                }

                std::vector<double> dts = key.evaluator.eval({ state.dsbKvs, state.usbKvs, glbPms });
                if (key.key == "") {
                    continue;
                }
                nmKeyPropsOut.push_back({ key.key, dts, key.outNum, &key });
            }

            if (skipSend) {
                continue;
            }

            std::vector<keyUpRec> keyNms;
            std::vector<INPUT> inputs;
            int finDefIntev = -1;
            int finUserIntev = -1;
            // 按下键
            for (int i = 0; i < nmKeyPropsOut.size(); i++) {
                auto& keyProp = nmKeyPropsOut[i];
                auto [intev, optInput] = KvInput::parseDownInput(keyProp.name, keyProp.props);
                if (!optInput.has_value()) {
                    continue;
                }
                finDefIntev = intev;
                finUserIntev = keyProp.out;
                keyNms.push_back({ keyProp.name, i });
                inputs.push_back(optInput.value());
                SimpleLogger::LOG_DEBUG("按下键: {}", keyProp.name);
                KvInput::setDown(keyProp.name);
            }
            if (state.macIntev == -1) {
                state.macIntev = finUserIntev > 0? finUserIntev : finDefIntev;
            }
            
            //LOG_INFO("Send Keys: {}", myjoin(keyNms, ","));
            UINT sentCount = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));

            inputs.clear();
            // 释放键
            for (int i = keyNms.size() - 1; i >= 0; i--) {
                auto& keyNm = keyNms[i];
                keyPropsOut& kpo = nmKeyPropsOut[keyNm.index];
                auto& keyDts = kpo.props;
                std::optional<INPUT> optInput = KvInput::parseUpInput(keyNm.name, keyDts);
                if (!optInput.has_value()) {
                    KvInput::setUp(keyNm.name);
                    continue;
                }
                if (kpo.keyExpr->upEval.isEmpty) {// 立即释放
                    inputs.push_back(optInput.value());
                    KvInput::setUp(keyNm.name);
                }
                else {// 延迟释放
                    SimpleLogger::LOG_INFO("加入延迟释放: {}", kpo.keyExpr->key);
                    delayUpKeys.push_back({ kpo.keyExpr->key, kpo.keyExpr->upEval, optInput.value()});
                }
            }
            if (inputs.size() > 0) {
                sentCount = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
            }

        }
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
        std::vector<std::vector<KeyExpr>> toKeysGroups;

        std::vector<KeyExpr> toKeys;
        int pos = 0;
        std::string name = "";
        std::string out = "";
        KeyExpr expr;
        while (pos < keysExpStr.size()) {
            char c = keysExpStr[pos];
            
            if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9') || c == '_') {
                name += c;
            } else {
                if (!name.empty()) {
                    expr.key = name;
                    if (name == "_delay") {
                        expr.type = 3;
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
                        } else if (c == ')') {
                            cnt--;
                            if (cnt == 0) {
                                std::string expStr = keysExpStr.substr(begPos + 1, pos - begPos - 1);
                                if (!expr.evaluator.setExpression(expStr)) {
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
                        if (('0' <= c && c <= '9')) {
                            out += c;
                        }
                        else {
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
                            case '{':
                                while (pos < keysExpStr.size()) {
                                    c = keysExpStr[pos];
                                    out += c;
                                    if (c == '}') {
                                        break;
                                    }
                                    pos++;
                                }
                                break;
                            case ',':
                                switch (groupIndex) {
                                    case 0: 
                                        if (!out.empty()) {
                                            expr.outNum = atoi(out.c_str());
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
                                }
                                else {
                                    breakFlag = true;
                                }
                                break;

                            }
                            if (breakFlag) {
                                break;
                            }
                        }
                        pos++;
                    }
                    if (!out.empty()) {
                        switch (groupIndex) {
                            case 0:
                                expr.outNum = atoi(out.c_str());
                                out.clear();
                                break;
                            case 1:
                                expr.upEval.setExpression(out);
                                break;
                            default:
                                break;
                        }
                    }
                }

                if (c == '$') {
                    expr.type = 4;
                    pos++;
                    continue;
                }

                if (c == '[') { // 循环开始
                    expr.type = 1;
                    expr.key = "[";
                    c = ',';
                }

                if (!expr.key.empty() || !expr.evaluator.isEmpty) {
                    toKeys.push_back(expr);
                    expr = KeyExpr();
                }

                if (c == ']') { // 结束
                    expr.type = 2;
                    expr.key = "]";
                    c = ',';
                }

                if (c == ',') { // 另一组按键
                    if (!toKeys.empty()) {
                        toKeysGroups.push_back(toKeys);
                        toKeys.clear();
                    }
                }
            }

            pos++;
        }
        if (!name.empty()) {
            expr.key = name;
        }
        if (!expr.key.empty()) {
            toKeys.push_back(expr);
        }
        if (!toKeys.empty()) {
            toKeysGroups.push_back(toKeys);
        }
        return toKeysGroups;
    }
public:
    // 短事件映射表：源按钮列表 -> 目标按键列表
    std::vector<RuleMatch> sm_ruleMatchs;
    std::vector<DNRecord> sm_downedNs;
    std::vector<std::vector<int>> ct_downedKs;
    std::map<std::string, int> glbPms;
    std::vector<DlUpKey> delayUpKeys;
    bool confFlag = false;
};
