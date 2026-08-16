#ifndef BILLING_MANAGER_H
#define BILLING_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

class BillingManager {
public:
    BillingManager();
    void begin();
    
    // 現在の累積電力量(0xE0: kWh)を受け取り、今月分(23日〜翌22日)の消費量を計算
    double updateAndGetMonthlyKWh(double currentTotalKWh);

    // 現在の期間文字列（例: "7/23 - 8/22"）
    String getPeriodString() const;
    
    // ベースとなる検針開始時点の総電力量
    double getBaseKWh() const { return _baseKWh; }
    
    // 手動で基準値を補正・設定する場合
    void setBaseKWh(double baseKWh);

private:
    Preferences _prefs;
    double _baseKWh = 0.0;
    int _savedStartYear = 0;
    int _savedStartMonth = 0;
    
    void getBillingCycle(int currentYear, int currentMonth, int currentDay, 
                         int& startYear, int& startMonth, int& endYear, int& endMonth) const;
};

#endif // BILLING_MANAGER_H
