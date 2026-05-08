// Single-page dashboard served at "/". Polls /api/live every 2 s.
#pragma once

namespace fw {

inline constexpr char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Air Quality Station</title>
<style>
:root{--bg:#0f1419;--card:#1a2129;--fg:#e6e6e6;--mut:#8b98a5;--good:#2ecc71;--moderate:#f1c40f;--poor:#e67e22;--unhealthy:#e74c3c;--hazardous:#8e44ad}
body{margin:0;font:16px system-ui,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:720px;margin:auto;padding:16px}
h1{font-size:20px;margin:0 0 12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.card{background:var(--card);border-radius:10px;padding:14px}
.lbl{color:var(--mut);font-size:13px}.val{font-size:30px;font-weight:600;margin-top:4px}
#aqi{border-left:6px solid var(--mut)}
.mut{color:var(--mut);font-size:13px;margin-top:12px}
form{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px;margin-top:16px}
input,select,button{font:inherit;padding:6px;border-radius:6px;border:1px solid #333;background:#0b0f13;color:var(--fg)}
button{background:#2d7ff9;border:0;cursor:pointer}
</style></head><body><main>
<h1>Air Quality Station</h1>
<div class="grid">
<div class="card" id="aqi"><div class="lbl">Air quality index</div><div class="val" id="v-aqi">--</div><div class="lbl" id="v-level"></div></div>
<div class="card"><div class="lbl">CO&#8322; (ppm)</div><div class="val" id="v-co2">--</div></div>
<div class="card"><div class="lbl">Temperature (&deg;C)</div><div class="val" id="v-t">--</div></div>
<div class="card"><div class="lbl">Humidity (%RH)</div><div class="val" id="v-rh">--</div></div>
<div class="card"><div class="lbl">Dew point (&deg;C)</div><div class="val" id="v-dew">--</div></div>
<div class="card"><div class="lbl">Comfort</div><div class="val" id="v-comfort">--</div></div>
</div>
<div class="mut" id="v-diag">waiting for first sample&hellip;</div>
<form id="cfg">
<label class="lbl">Target IP<input name="ip"></label>
<label class="lbl">Port<input name="port" type="number" min="1" max="65535"></label>
<label class="lbl">Period (ms)<input name="period_ms" type="number" min="1000" max="3600000"></label>
<label class="lbl">Format<select name="format"><option>binary</option><option>json</option><option>both</option></select></label>
<label class="lbl">Altitude (m)<input name="altitude_m" type="number" min="0" max="3000"></label>
<label class="lbl">Filter window<input name="window" type="number" min="1" max="32"></label>
<button type="submit">Save</button><span class="lbl" id="v-msg"></span>
</form>
</main><script>
const $=id=>document.getElementById(id);
const fmt=(v,d)=>v===null||v===undefined?'--':Number(v).toFixed(d);
async function live(){try{const r=await fetch('/api/live');if(!r.ok)return;const j=await r.json();
$('v-aqi').textContent=j.aqi;$('v-level').textContent=j.level;$('aqi').style.borderLeftColor='var(--'+j.level+')';
$('v-co2').textContent=fmt(j.co2_ppm,0);$('v-t').textContent=fmt(j.temp_c,1);$('v-rh').textContent=fmt(j.rh_pct,1);
$('v-dew').textContent=fmt(j.dew_c,1);$('v-comfort').textContent=(j.flags&8)?'yes':'no';
$('v-diag').textContent='seq '+j.seq+' · uptime '+Math.round(j.ts_ns/1e9)+' s · sensor errors '+j.errors+
' · RSSI '+j.rssi_dbm+' dBm · UDP sent '+j.udp_sent+' failed '+j.udp_failed+((j.flags&4)?' · SHT31 fallback':'');
}catch(e){$('v-diag').textContent='offline';}}
async function loadCfg(){const r=await fetch('/api/config');const j=await r.json();for(const k in j){const e=$('cfg').elements[k];if(e)e.value=j[k];}}
$('cfg').addEventListener('submit',async ev=>{ev.preventDefault();const body=new URLSearchParams(new FormData($('cfg')));
const r=await fetch('/api/config',{method:'POST',body});$('v-msg').textContent=r.ok?'saved':await r.text();});
loadCfg();live();setInterval(live,2000);
</script></body></html>
)HTML";

}  // namespace fw
