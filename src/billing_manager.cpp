#include "billing_manager.h"

BillingManager::BillingManager() {}

void BillingManager::begin() {
    _prefs.begin("broute_bill", false);
    _baseKWh = _prefs.getDouble("base_kwh", 0.0);
    _savedStartYear = _prefs.getInt("start_year", 0);
    _savedStartMonth = _prefs.getInt("start_month", 0);
    Serial.printf("[BillingManager] Loaded: Base=%.1f kWh, Cycle=%d/%d(23rd)\n", 
                  _baseKWh, _savedStartYear, _savedStartMonth);
}

void BillingManager::getBillingCycle(int curYear, int curMonth, int curDay, 
                                    int& startYear, int& startMonth, int& endYear, int& endMonth) const {
    if (curDay >= 23) {
        // 当月23日 〜 翌月22日
        startYear = curYear;
        startMonth = curMonth;

        if (curMonth == 12) {
            endYear = curYear + 1;
            endMonth = 1;
        } else {
            endYear = curYear;
            endMonth = curMonth + 1;
        }
    } else {
        // 前月23日 〜 当月22日
        endYear = curYear;
        endMonth = curMonth;

        if (curMonth == 1) {
            startYear = curYear - 1;
            startMonth = 12;
        } else {
            startYear = curYear;
            startMonth = curMonth - 1;
        }
    }
}

String BillingManager::getPeriodString() const {
    time_t nowTime;
    time(&nowTime);
    struct tm timeinfo;
    if (!localtime_r(&nowTime, &timeinfo) || timeinfo.tm_year < (2020 - 1900)) {
        return "23rd - 22nd";
    }

    int curYear = timeinfo.tm_year + 1900;
    int curMonth = timeinfo.tm_mon + 1;
    int curDay = timeinfo.tm_mday;

    int sY, sM, eY, eM;
    getBillingCycle(curYear, curMonth, curDay, sY, sM, eY, eM);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d - %d/%d", sM, 23, eM, 22);
    return String(buf);
}

double BillingManager::updateAndGetMonthlyKWh(double currentTotalKWh) {
    if (currentTotalKWh <= 0.0) return 0.0;

    time_t nowTime;
    time(&nowTime);
    struct tm timeinfo;
    if (!localtime_r(&nowTime, &timeinfo) || timeinfo.tm_year < (2020 - 1900)) {
        // NTP未同期時は単純差分または暫定値
        if (_baseKWh > 0.0 && currentTotalKWh >= _baseKWh) {
            return currentTotalKWh - _baseKWh;
        }
        return 0.0;
    }

    int curYear = timeinfo.tm_year + 1900;
    int curMonth = timeinfo.tm_mon + 1;
    int curDay = timeinfo.tm_mday;

    int currentCycleStartYear, currentCycleStartMonth, eY, eM;
    getBillingCycle(curYear, curMonth, curDay, currentCycleStartYear, currentCycleStartMonth, eY, eM);

    // 新しい検針期間に入った（または初回記録）の場合、ベース値を更新
    bool isNewCycle = (_savedStartYear != currentCycleStartYear || _savedStartMonth != currentCycleStartMonth);
    
    if (_baseKWh <= 0.0 || isNewCycle) {
        _baseKWh = currentTotalKWh;
        _savedStartYear = currentCycleStartYear;
        _savedStartMonth = currentCycleStartMonth;

        _prefs.putDouble("base_kwh", _baseKWh);
        _prefs.putInt("start_year", _savedStartYear);
        _prefs.putInt("start_month", _savedStartMonth);
        
        Serial.printf("[BillingManager] New Cycle (%d/%d/23) Base Set: %.1f kWh\n", 
                      _savedStartYear, _savedStartMonth, _baseKWh);
    }

    double monthlyUsage = currentTotalKWh - _baseKWh;
    return (monthlyUsage >= 0.0) ? monthlyUsage : 0.0;
}

void BillingManager::setBaseKWh(double baseKWh) {
    _baseKWh = baseKWh;
    _prefs.putDouble("base_kwh", _baseKWh);
}
