#pragma once
class WifiManager {
public:
    static WifiManager& GetInstance() { static WifiManager instance; return instance; }
    bool IsConnected() const { return true; }
};
