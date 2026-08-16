#include "web_server.h"
#include <ArduinoJson.h>

PowerWebServer::PowerWebServer(WiSunBroute& broute, PowerHistory& history, BillingManager& billing) 
    : _broute(broute), _history(history), _billing(billing) {}

PowerWebServer::~PowerWebServer() {
    if (_server) {
        delete _server;
        _server = nullptr;
    }
}

void PowerWebServer::begin() {
    if (_server == nullptr) {
        _server = new AsyncWebServer(80);
    }
    setupRoutes();
    _server->begin();
    Serial.println("Web Server Started on port 80.");
}

void PowerWebServer::setupRoutes() {
    if (!_server) return;

    // 1. トップページ Web UI
    _server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "text/html", getHtmlContent());
    });

    // 2. リアルタイム JSON API (/api/power)
    _server->on("/api/power", HTTP_GET, [this](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> doc;
        const PowerData& data = _broute.getLatestData();
        
        int32_t wattR = data.validCurrent ? data.wattRPhase : (data.instantaneousWatt / 2);
        int32_t wattT = data.validCurrent ? data.wattTPhase : (data.instantaneousWatt - wattR);

        double totalKWh = data.validCumulative ? data.cumulativeKWh : 0.0;
        double monthlyKWh = _billing.updateAndGetMonthlyKWh(totalKWh);

        doc["watt"] = data.validInstantaneous ? data.instantaneousWatt : 0;
        doc["watt_r"] = data.validInstantaneous ? wattR : 0;
        doc["watt_t"] = data.validInstantaneous ? wattT : 0;
        doc["amp_r"] = data.validCurrent ? data.currentRPhase : 0.0f;
        doc["amp_t"] = data.validCurrent ? data.currentTPhase : 0.0f;
        doc["kwh_total"] = totalKWh;
        doc["kwh_monthly"] = monthlyKWh;
        doc["period"] = _billing.getPeriodString();
        doc["state"] = _broute.getStateString();
        doc["connected"] = (_broute.getState() == BrouteState::CONNECTED);
        doc["uptime_sec"] = millis() / 1000;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // 3. 24時間高密度履歴 JSON API (/api/history)
    _server->on("/api/history", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "application/json", _history.toJson());
    });
}

String PowerWebServer::getHtmlContent() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>B-Route Power Dashboard</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-dark: #0b0f19;
            --card-bg: #111827;
            --card-border: #1f2937;
            --text-main: #f3f4f6;
            --text-sub: #9ca3af;
            --c-0xe7: #f43f5e;  /* 全体電力: ローズピンク */
            --c-r: #38bdf8;     /* R相: シアン */
            --c-t: #fbbf24;     /* T相: イエロー/アンバー */
            --c-month: #a78bfa; /* 今月積算: パープル */
        }

        * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Outfit', sans-serif; }
        body { background: var(--bg-dark); color: var(--text-main); min-height: 100vh; padding: 18px; }
        .container { max-width: 1100px; margin: 0 auto; }

        header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
        h1 { font-size: 1.5rem; font-weight: 700; color: #f9fafb; display: flex; align-items: center; gap: 8px; }
        
        .badge { padding: 5px 12px; border-radius: 16px; font-size: 0.8rem; font-weight: 600; background: var(--card-bg); border: 1px solid var(--card-border); font-family: 'JetBrains Mono', monospace; }
        .status-connected { color: #34d399; border-color: rgba(52, 211, 153, 0.4); }

        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(210px, 1fr)); gap: 14px; margin-bottom: 20px; }
        .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 12px; padding: 18px; box-shadow: 0 4px 20px rgba(0,0,0,0.5); }
        .card-title { font-size: 0.8rem; color: var(--text-sub); margin-bottom: 4px; text-transform: uppercase; letter-spacing: 0.5px; }
        .card-val { font-size: 2.1rem; font-weight: 700; font-family: 'JetBrains Mono', monospace; }
        .card-unit { font-size: 0.95rem; color: var(--text-sub); margin-left: 4px; }
        .card-sub { font-size: 0.75rem; color: var(--c-month); margin-top: 4px; font-family: 'JetBrains Mono', monospace; }

        .val-0xe7 { color: var(--c-0xe7); }
        .val-r { color: var(--c-r); }
        .val-t { color: var(--c-t); }
        .val-month { color: var(--c-month); }

        .chart-container-card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 12px; padding: 20px; margin-bottom: 20px; }
        .chart-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
        .chart-title { font-size: 1.05rem; font-weight: 600; color: #f3f4f6; }
        
        .chart-box { height: 360px; position: relative; width: 100%; }
        canvas { width: 100% !important; height: 100% !important; }

        /* Grafana風の統計サマリーテーブル */
        .grafana-table { width: 100%; border-collapse: collapse; margin-top: 16px; font-family: 'JetBrains Mono', monospace; font-size: 0.78rem; }
        .grafana-table th { text-align: right; padding: 6px 10px; color: var(--text-sub); border-bottom: 1px solid var(--card-border); font-weight: 600; }
        .grafana-table th:first-child { text-align: left; }
        .grafana-table td { padding: 8px 10px; border-bottom: 1px solid rgba(255,255,255,0.04); text-align: right; }
        .grafana-table td:first-child { text-align: left; font-family: 'Outfit', sans-serif; font-size: 0.85rem; }
        .dot { display: inline-block; width: 10px; height: 10px; border-radius: 2px; margin-right: 6px; }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>⚡ スマートメーター Bルート 電力モニタ</h1>
            <div id="statusBadge" class="badge">Connecting...</div>
        </header>

        <!-- メインメトリクスカード -->
        <div class="grid">
            <div class="card">
                <div class="card-title">0xE7 全体瞬時電力</div>
                <div><span id="wattVal" class="card-val val-0xe7">---</span><span class="card-unit">W</span></div>
            </div>
            <div class="card">
                <div class="card-title">0xE8 R相 電流 / 電力 (100V)</div>
                <div><span id="ampRVal" class="card-val val-r">--.-</span><span class="card-unit">A</span> <span id="wattRVal" style="font-size:1.1rem; color:var(--text-sub); margin-left:4px;">(--W)</span></div>
            </div>
            <div class="card">
                <div class="card-title">0xE8 T相 電流 / 電力 (100V)</div>
                <div><span id="ampTVal" class="card-val val-t">--.-</span><span class="card-unit">A</span> <span id="wattTVal" style="font-size:1.1rem; color:var(--text-sub); margin-left:4px;">(--W)</span></div>
            </div>
            <div class="card">
                <div class="card-title">今月の積算電力量</div>
                <div><span id="monthlyKwhVal" class="card-val val-month">---</span><span class="card-unit">kWh</span></div>
                <div id="periodLabel" class="card-sub">Date range: 23rd - 22nd</div>
            </div>
        </div>

        <!-- 2軸 Grafana風 電力・電流時系列グラフ (0:00 〜 24:00) -->
        <div class="chart-container-card">
            <div class="chart-header">
                <div class="chart-title">低圧スマート電力量メータ 瞬時電力・相別電流計測値 (00:00 - 24:00)</div>
            </div>
            
            <div class="chart-box">
                <canvas id="grafanaChart"></canvas>
            </div>

            <!-- Grafana Legend & 統計テーブル -->
            <table class="grafana-table">
                <thead>
                    <tr>
                        <th>Series</th>
                        <th>Min</th>
                        <th>Avg</th>
                        <th>Max</th>
                        <th>Last</th>
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td><span class="dot" style="background:var(--c-0xe7);"></span> <strong>0xE7 全体瞬時電力 (右軸)</strong></td>
                        <td id="stat_0xe7_min">-</td>
                        <td id="stat_0xe7_avg">-</td>
                        <td id="stat_0xe7_max">-</td>
                        <td id="stat_0xe7_last" style="color:var(--c-0xe7); font-weight:700;">-</td>
                    </tr>
                    <tr>
                        <td><span class="dot" style="background:var(--c-r);"></span> <strong>0xE8 R相 電流 (左軸)</strong></td>
                        <td id="stat_r_min">-</td>
                        <td id="stat_r_avg">-</td>
                        <td id="stat_r_max">-</td>
                        <td id="stat_r_last" style="color:var(--c-r); font-weight:700;">-</td>
                    </tr>
                    <tr>
                        <td><span class="dot" style="background:var(--c-t);"></span> <strong>0xE8 T相 電流 (左軸)</strong></td>
                        <td id="stat_t_min">-</td>
                        <td id="stat_t_avg">-</td>
                        <td id="stat_t_max">-</td>
                        <td id="stat_t_last" style="color:var(--c-t); font-weight:700;">-</td>
                    </tr>
                </tbody>
            </table>
        </div>
    </div>

    <script>
        const ctx = document.getElementById('grafanaChart').getContext('2d');
        const grafanaChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: '0xE7 全体電力 (W)',
                        data: [],
                        borderColor: '#f43f5e',
                        backgroundColor: 'rgba(244, 63, 94, 0.08)',
                        borderWidth: 2,
                        fill: true,
                        tension: 0.2,
                        pointRadius: 1.5,
                        yAxisID: 'yWatt'
                    },
                    {
                        label: '0xE8 R相電流 (A)',
                        data: [],
                        borderColor: '#38bdf8',
                        borderWidth: 2,
                        fill: false,
                        tension: 0.2,
                        pointRadius: 1.5,
                        yAxisID: 'yAmp'
                    },
                    {
                        label: '0xE8 T相電流 (A)',
                        data: [],
                        borderColor: '#fbbf24',
                        borderWidth: 2,
                        fill: false,
                        tension: 0.2,
                        pointRadius: 1.5,
                        yAxisID: 'yAmp'
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: { mode: 'index', intersect: false },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        backgroundColor: '#1f2937',
                        titleColor: '#f3f4f6',
                        bodyColor: '#e5e7eb',
                        borderColor: '#374151',
                        borderWidth: 1,
                        padding: 10,
                        callbacks: {
                            label: function(c) {
                                if (c.dataset.yAxisID === 'yWatt') return ` 全体電力: ${c.raw} W`;
                                return ` ${c.dataset.label}: ${c.raw} A (${Math.round(c.raw * 100)} W)`;
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        grid: { color: 'rgba(255, 255, 255, 0.05)' },
                        ticks: { color: '#9ca3af', maxRotation: 0, autoSkip: true, maxTicksLimit: 14 }
                    },
                    yAmp: {
                        type: 'linear',
                        position: 'left',
                        beginAtZero: true,
                        suggestedMax: 30,
                        grid: { color: 'rgba(255, 255, 255, 0.06)' },
                        ticks: { color: '#9ca3af', callback: v => v + ' A' },
                        title: { display: true, text: '電流 (A)', color: '#9ca3af', font: { size: 11 } }
                    },
                    yWatt: {
                        type: 'linear',
                        position: 'right',
                        beginAtZero: true,
                        suggestedMax: 3000,
                        grid: { drawOnChartArea: false },
                        ticks: { color: '#f43f5e', callback: v => v >= 1000 ? (v/1000).toFixed(1) + ' kW' : v + ' W' },
                        title: { display: true, text: '全体電力 (W)', color: '#f43f5e', font: { size: 11 } }
                    }
                }
            }
        });

        function calcStats(arr) {
            const valid = arr.filter(v => v !== null && v !== undefined);
            if (valid.length === 0) return { min: '-', avg: '-', max: '-', last: '-' };
            const min = Math.min(...valid);
            const max = Math.max(...valid);
            const sum = valid.reduce((a, b) => a + b, 0);
            const avg = (sum / valid.length).toFixed(1);
            const last = valid[valid.length - 1];
            return { min, avg, max, last };
        }

        async function updateRealtime() {
            try {
                const res = await fetch('/api/power');
                const data = await res.json();

                document.getElementById('wattVal').innerText = data.watt;
                document.getElementById('ampRVal').innerText = data.amp_r.toFixed(1);
                document.getElementById('wattRVal').innerText = `(${data.watt_r}W)`;
                document.getElementById('ampTVal').innerText = data.amp_t.toFixed(1);
                document.getElementById('wattTVal').innerText = `(${data.watt_t}W)`;
                document.getElementById('monthlyKwhVal').innerText = data.kwh_monthly.toFixed(1);
                document.getElementById('periodLabel').innerText = 'Date range: ' + data.period;
                
                const badge = document.getElementById('statusBadge');
                badge.innerText = data.state;
                badge.className = data.connected ? 'badge status-connected' : 'badge';
            } catch (err) {
                console.error('Fetch power error:', err);
            }
        }

        async function updateHistory() {
            try {
                const res = await fetch('/api/history');
                const history = await res.json();

                const labels = [];
                const dataWatt = [];
                const dataAmpR = [];
                const dataAmpT = [];

                history.slots.forEach(s => {
                    labels.push(s.time);
                    if (s.recorded) {
                        dataWatt.push(s.watt);
                        dataAmpR.push(s.amp_r);
                        dataAmpT.push(s.amp_t);
                    } else {
                        dataWatt.push(null);
                        dataAmpR.push(null);
                        dataAmpT.push(null);
                    }
                });

                grafanaChart.data.labels = labels;
                grafanaChart.data.datasets[0].data = dataWatt;
                grafanaChart.data.datasets[1].data = dataAmpR;
                grafanaChart.data.datasets[2].data = dataAmpT;
                grafanaChart.update();

                const statWatt = calcStats(dataWatt);
                const statR = calcStats(dataAmpR);
                const statT = calcStats(dataAmpT);

                document.getElementById('stat_0xe7_min').innerText = statWatt.min !== '-' ? statWatt.min + ' W' : '-';
                document.getElementById('stat_0xe7_avg').innerText = statWatt.avg !== '-' ? statWatt.avg + ' W' : '-';
                document.getElementById('stat_0xe7_max').innerText = statWatt.max !== '-' ? statWatt.max + ' W' : '-';
                document.getElementById('stat_0xe7_last').innerText = statWatt.last !== '-' ? statWatt.last + ' W' : '-';

                document.getElementById('stat_r_min').innerText = statR.min !== '-' ? statR.min + ' A' : '-';
                document.getElementById('stat_r_avg').innerText = statR.avg !== '-' ? statR.avg + ' A' : '-';
                document.getElementById('stat_r_max').innerText = statR.max !== '-' ? statR.max + ' A' : '-';
                document.getElementById('stat_r_last').innerText = statR.last !== '-' ? statR.last + ' A' : '-';

                document.getElementById('stat_t_min').innerText = statT.min !== '-' ? statT.min + ' A' : '-';
                document.getElementById('stat_t_avg').innerText = statT.avg !== '-' ? statT.avg + ' A' : '-';
                document.getElementById('stat_t_max').innerText = statT.max !== '-' ? statT.max + ' A' : '-';
                document.getElementById('stat_t_last').innerText = statT.last !== '-' ? statT.last + ' A' : '-';
            } catch (err) {
                console.error('Fetch history error:', err);
            }
        }

        setInterval(updateRealtime, 3000);
        setInterval(updateHistory, 20000);

        updateRealtime();
        updateHistory();
    </script>
</body>
</html>
)rawliteral";
}
