#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include "wisun_broute.h"
#include "power_history.h"
#include "billing_manager.h"

class PowerWebServer {
public:
    PowerWebServer(WiSunBroute& broute, PowerHistory& history, BillingManager& billing);
    ~PowerWebServer();
    void begin();

private:
    AsyncWebServer* _server = nullptr;
    WiSunBroute& _broute;
    PowerHistory& _history;
    BillingManager& _billing;

    void setupRoutes();
    String getHtmlContent();
};

#endif // WEB_SERVER_H
