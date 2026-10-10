#pragma once
#include <map>
#include <memory>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <set>
#include <queue>

#include "IConnection.h"
#include "CMGamepadConnection.h"
#include "JoyConConnection.h"

class ConnManage : public IConnection {
private:
    int defInterval = 0;
    int interval = defInterval;
    EvState state;
    std::thread m_readThread;
    std::function<void(const EvState&)> m_onStateChanged;
    std::set<unsigned long long> runedDrivers;
    std::string _nmMac = "ConnManage";
public:
    ConnManage() = default;
    ~ConnManage() override {
    }
    std::string getAliasNm() override {
        return _nmMac;
    }
    std::string getNmMac() override {
        std::string allNmMac = "";
        int i = 0;
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        for (std::unique_ptr<IConnection>& conn : m_connections) {
            if (i != 0) {
                allNmMac += "\\";
            }
            allNmMac += conn->getNmMac();
            i++;
        }
        return allNmMac;
    }
    unsigned long long getMac() override {
        return 0;
    }
    void setOnStateChanged(std::function<void(const EvState&)> callback) override {
        m_onStateChanged = callback;
    }
    void setOnConnectionChanged(std::function<void(bool)> callback) override {
    }

    int offSignBase(int off) override {
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        for (std::unique_ptr<IConnection>& conn : m_connections) {
            off = conn->offSignBase(off);
        }
        return off;
    }
    std::string devChooseNmMac(std::string name, unsigned long long macAddress) override {
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        std::vector<std::string> nmMacs;
        for (std::unique_ptr<IConnection>& conn : m_connections) {
            nmMacs.push_back(conn->getNmMac());
        }

        for (auto& driver : IConnection::m_drivers) {
            if (driver.matcher(name)) {
                std::unique_ptr<IConnection> conn = driver.factory();
                std::string tmpNmMac = conn->devChooseNmMac(name, macAddress);
                if (std::find(nmMacs.begin(), nmMacs.end(), tmpNmMac) == nmMacs.end()) {
                    nmMacs.push_back(tmpNmMac);
                }
                break;
            }
        }

        std::sort(nmMacs.begin(), nmMacs.end(), [](std::string& nmMacA, std::string& nmMacB) {
            return nmMacA < nmMacB;
        });

        std::string allNmMac = "";
        int i = 0;
        for (std::string& nmMac : nmMacs) {
            if (i != 0) {
                allNmMac += "\\";
            }
            allNmMac += nmMac;
            i++;
        }

        return allNmMac;
    }
    bool devConnect(std::string name, unsigned long long macAddress, std::wstring devId) override {
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        for (DriverInfo& driver : IConnection::m_drivers) {
            if (driver.matcher(name)) {
                std::unique_ptr<IConnection> conn = driver.factory();
                SimpleLogger::LOG_INFO("devConnect -> conn");
                if (conn->devConnect(name, macAddress, devId)) {
                    m_connections.emplace_back(std::move(conn));
                    std::sort(m_connections.begin(), m_connections.end(), [](std::unique_ptr<IConnection>& connA, std::unique_ptr<IConnection>& connB){
                        return connA->getNmMac() < connB->getNmMac();
                    });
                    return true;
                }
            }
        }

        return false;
    }

    void devDisconnect(std::string name, unsigned long long macAddress) override {
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();

        for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
            if ((*it)->getMac() == macAddress) {
                (*it)->devDisconnect(name, macAddress);
                m_connections.erase(it);
                break;
            }
        }
    }

    bool setDataView(std::string name, unsigned long long macAddress) override {
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        
        for (std::unique_ptr<IConnection>& conn : m_connections) {
            if (conn->getMac() == macAddress) {
                conn->setDataView(name, macAddress);
                break;
            }
        }

        DebugHexView::GetInstance().open();

        return false;
    }

    void indexSign(const std::set<std::string>& nameSet, SignCode& retDSign, SignCode& retUSign) override {
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        for (std::unique_ptr<IConnection>& conn : m_connections) {
            conn->indexSign(nameSet, retDSign, retUSign);
        }
    }

    void startReadThread() {
        m_readThread = std::thread(&ConnManage::readLoop, this, std::ref(state));
    }
    int readLoop(EvState& state) override {
        int tryinterval = interval > 10 ? interval : 10;
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        while (true) {
            state.sClear();
            std::queue<IConnection*> que;
            for (std::unique_ptr<IConnection>& conn : m_connections) {
                int retType = conn->readLoop(state);
                if (retType == 2) {
                    que.push(conn.get());
                }
            }

            int size = que.size();
            int sizeCnt = size;
            int timeCnt = 0;
            while (!que.empty()) {
                if (sizeCnt == size) {
                    Sleep(tryinterval);
                    timeCnt += tryinterval;
                    if (timeCnt > 1000) {
                        break;
                    }
                }
                IConnection* connPtr = que.front();
                que.pop();
                int retType = connPtr->readLoop(state);
                SimpleLogger::LOG_INFO("try reread");
                if (retType == 2) {
                    que.push(connPtr);
                }

                sizeCnt--;
                if (sizeCnt == 0) {
                    size = que.size();
                    sizeCnt = size;
                }
            }

            m_onStateChanged(state);
            Sleep(interval);
        }

        return 1;
    }

    void reloadConf(std::map<std::string, std::map<std::string, std::string>>& confs) override {
        std::map<std::string, std::string>& conf = confs[_nmMac];
        if (conf.find("interval") != conf.end()) {
            interval = std::stoi(conf["interval"]);
        }
        else {
            interval = defInterval;
        }

        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        for (std::unique_ptr<IConnection>& conn : m_connections) {
            conn->reloadConf(confs);
        }
    }

    void getDefConf(std::map<std::string, std::map<std::string, std::string>>& defConfs) override {
        std::map<std::string, std::string> defConf;
        defConf.insert(std::make_pair("interval", std::to_string(defInterval)));
        defConfs[_nmMac] = defConf;

        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        for (std::unique_ptr<IConnection>& conn : m_connections) {
            conn->getDefConf(defConfs);
        }
    }
    std::string getDefRule(std::string name, unsigned long long macAddress) override {
        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();

        for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
            if ((*it)->getMac() == macAddress) {
                return (*it)->getDefRule(name, macAddress);
            }
        }

        return "";
    }

    struct DevBtnIndInfo {
        int index;
        std::string aliasNm;
    };
    std::vector<std::string> getDevBtns(int cnt) override {
        std::vector<std::string> devBtns;
        std::map<std::string, DevBtnIndInfo> devBtnsIndex;

        std::vector<std::unique_ptr<IConnection>>& m_connections = getConnections();
        int size = m_connections.size();
        for (std::unique_ptr<IConnection>& conn : m_connections) {
            std::string aliasNm = conn->getAliasNm();
            std::vector<std::string> btns = conn->getDevBtns(size);
            for (std::string btn : btns) {
                if (devBtnsIndex.count(btn) > 0) {
                    DevBtnIndInfo& devBtnInfo = devBtnsIndex[btn];
                    if (devBtnInfo.index >= 0) {
                        devBtns[devBtnInfo.index] = devBtns[devBtnInfo.index] + "@" + devBtnInfo.aliasNm;
                        devBtnInfo.index = -1;
                    }
                    devBtns.push_back(btn + "@" + aliasNm);
                }
                else {
                    devBtns.push_back(btn);
                    devBtnsIndex[btn] = DevBtnIndInfo{ static_cast<int>(devBtns.size() - 1), aliasNm };
                }
            }
        }

        return devBtns;
    }

private:
    static std::vector<std::unique_ptr<IConnection>>& getConnections() {
        static std::vector<std::unique_ptr<IConnection>> connections;
        return connections;
    }
};