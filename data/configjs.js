
// ============== Helpers DOM & UI ==============
const $ = (id) => document.getElementById(id);
const on = (id, evt, handler) => { const el = $(id); if (el) el.addEventListener(evt, handler); };
const setText = (id, v) => { const el = $(id); if (el) el.textContent = (v ?? '—'); };
const fmt = (v, d = 1) => (v === null || v === undefined || Number.isNaN(v)) ? '--' : Number(v).toFixed(d);
const toast = (msg, ok = true) => {
  let t = $('toast'); if (!t) { console.warn('toast element missing'); return; }
  t.textContent = msg;
  t.style.border = `1px solid ${ok ? '#29c14c' : '#e74c3c'}`;
  t.style.display = 'block'; setTimeout(() => t.style.display = 'none', 2500);
};
const degToCompass = (deg) => (deg == null || Number.isNaN(deg)) ? '—' : ([
  "N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"
][Math.floor(((deg % 360) / 22.5) + 0.5) % 16]);

// ============== HTTP utils ==============
async function getJSON(url) {
  const res = await fetch(url, { cache: 'no-store' });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}
async function postForm(url, obj) {
  const fd = new URLSearchParams();
  Object.entries(obj).forEach(([k,v]) => { if (v !== undefined && v !== null) fd.append(k, v); });
  const res = await fetch(url, { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:fd.toString() });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.text();
}

// ============== Chargement config (API + modules + NTP/TZ) ==============
async function loadConfig() {
  try {
    const cfg = await getJSON('/config.json');   // adresse_api, token, activation_envoi_api, (optionnel ntp_server/timezone)
    if ($('api_url'))   $('api_url').value   = cfg.adresse_api || '';
    if ($('api_token')) $('api_token').value = cfg.token || '';
    if ($('api_enabled')) $('api_enabled').checked = !!cfg.activation_envoi_api;

    const st = await getJSON('/status.json');    // modules + conf(ntp_server, timezone) + réseau
    if ($('bmp280_enabled'))   $('bmp280_enabled').checked = !!st.modules?.bmp280;
    if ($('dht22_enabled'))    $('dht22_enabled').checked  = !!st.modules?.dht22;
    if ($('sht40_enabled'))    $('sht40_enabled').checked  = !!st.modules?.sht40;
    if ($('anemo_enabled'))    $('anemo_enabled').checked  = !!st.modules?.anemo;
    if ($('vane_enabled'))     $('vane_enabled').checked   = !!st.modules?.vane;
    if ($('rain_enabled'))     $('rain_enabled').checked   = !!st.modules?.rain;
    if ($('tension_enabled'))  $('tension_enabled').checked= !!st.modules?.tension;
    if ($('bitvie_enabled'))   $('bitvie_enabled').checked = !!st.modules?.bitvie;

    if ($('ntp_server')) $('ntp_server').value = st.conf?.ntp_server ?? (cfg.ntp_server ?? 'pool.ntp.org');
    if ($('timezone'))   $('timezone').value   = st.conf?.timezone   ?? (cfg.timezone   ?? 'Europe/Paris');

    toast('Configuration chargée ✔️');
  } catch(e) {
    console.error('loadConfig error:', e);
    toast(`Erreur chargement config: ${e.message}`, false);
  }
}

// ============== Sauvegardes ==============
async function saveApi(e) {
  if (e?.preventDefault) e.preventDefault();
  try {
    const enabled = $('api_enabled')?.checked;
    const payload = {
      api_url: $('api_url')?.value?.trim() ?? '',
      api_token: $('api_token')?.value?.trim() ?? '',
      ...(enabled ? { api_enabled: 'on' } : {}) // présence => activé ; absence => désactivé
    };
    await postForm('/config/api', payload);
    toast('API enregistrée ✔️');
  } catch(err) {
    console.error('saveApi error:', err);
    toast(`Erreur enregistrement API: ${err.message}`, false);
  }
}

async function saveModules(e) {
  if (e?.preventDefault) e.preventDefault();
  const checked = id => $(id)?.checked ? 'on' : undefined;
  try {
    const payload = {
      bmp280_enabled:  checked('bmp280_enabled'),
      dht22_enabled:   checked('dht22_enabled'),
      sht40_enabled:   checked('sht40_enabled'),
      anemo_enabled:   checked('anemo_enabled'),
      vane_enabled:    checked('vane_enabled'),
      rain_enabled:    checked('rain_enabled'),
      tension_enabled: checked('tension_enabled'),
      bitvie_enabled:  checked('bitvie_enabled'),
    };
    await postForm('/config/modules', payload);
    toast('Modules enregistrés ✔️');
  } catch(err) {
    console.error('saveModules error:', err);
    toast(`Erreur enregistrement modules: ${err.message}`, false);
  }
}

async function setTimeNow() {
  try {
    // Optionnel: persister ntp/tz via /config.save (si gérés côté ESP32)
    const ntp = $('ntp_server')?.value?.trim() || 'pool.ntp.org';
    const tz  = $('timezone')?.value?.trim()   || 'Europe/Paris';
    await postForm('/config.save', { ntp_server: ntp, timezone: tz });
  } catch(e) {
    console.warn('ntp/timezone save optional failed:', e.message);
  }

  try {
    const res = await fetch('/settime', { method: 'GET' });
    const txt = await res.text();
    setText('ntpResult', `Résultat : ${txt}`);
    toast('RTC mise à l’heure ✔️');
  } catch(err) {
    console.error('setTime error:', err);
    setText('ntpResult', `Résultat : ${err.message}`);
    toast(`Erreur NTP/RTC: ${err.message}`, false);
  }
}

// ============== Visu live (proche accueil) ==============
async function refreshLive() {
  try {
    const d = await getJSON('/data');
    setText('lastTs', d.datetime);
    setText('tpsVie', fmt(d.tpsvie,0));
    setText('tempBmp', fmt(d.tempbmp280,1));
    setText('pressBmp', fmt(d.pression,1));
    setText('tempDht', fmt(d.temperature,1));
    setText('humDht', fmt(d.humidite,0));
    setText('ventInst', fmt(d.anemometre,1));
    setText('ventGust', fmt(d.rafale,1));
    setText('ventAvg', fmt(d.moyenne10min,1));
    setText('dirDeg', fmt(d.direction,0));
    setText('dirCard', degToCompass(Number(d.direction)));
    setText('rainH', fmt(d.pluieHeure,2));
    setText('rainD', fmt(d.pluieJour,2));
    setText('rainW', fmt(d.pluieSemaine,2));
    setText('vb', fmt(d.tension_batterie,2));
    setText('vs', fmt(d.tension_solaire,2));

    const pct = Number(d.wifi_percent ?? 0);
    setText('wifiRssi', d.wifi_rssi ?? '--');
    setText('wifiPct', pct);
    setText('wifiBars', d.wifi_bars ?? '--');
  } catch(err) {
    console.error('refreshLive error:', err);
    toast(`Erreur live: ${err.message}`, false);
  }

  try {
    const ec = await getJSON('/etatcapteurs');
    const badge = (ok) => ok ? 'badge ok' : 'badge bad';
    const setBadge = (id, ok) => { const el = $(id); if (el) { el.className = badge(!!ok); el.textContent = ok ? 'OK' : 'HS'; } };

    setBadge('stBmp',  ec.etat?.bmp280);
    setBadge('stDht',  ec.etat?.dht22);
    // SHT40: seulement si module activé
    if ($( 'stSht')) {
      const active = !!ec.modules?.sht40;
      if (active) setBadge('stSht', ec.etat?.sht40);
      else { $('stSht').className = 'badge'; $('stSht').textContent = '—'; }
    }
    setBadge('stAnemo', ec.etat?.anemo);
    setBadge('stVane',  ec.etat?.girou);
    setBadge('stRain',  ec.etat?.pluvio);
  } catch(e) {
    console.warn('etatcapteurs fetch error:', e.message);
  }
}

// ============== Boot ==============
document.addEventListener('DOMContentLoaded', () => {
  // Boutons (nouvelle page)
  on('btnSaveApi', 'click', saveApi);
  on('btnSaveModules', 'click', saveModules);
  on('setTimeBtn', 'click', setTimeNow);
  on('refreshStatusBtn', 'click', loadConfig);
  on('testApiBtn', 'click', async () => {
    try { await getJSON('/status.json'); toast('Status OK ✔️'); }
    catch(e) { toast(`Status KO: ${e.message}`, false); }
  });

  // Compat héritée (anciennes versions à base de <form>)
  on('apiForm', 'submit', (e) => { e.preventDefault(); saveApi(e); });
  on('modulesForm', 'submit', (e) => { e.preventDefault(); saveModules(e); });

  loadConfig();
  refreshLive();
  setInterval(refreshLive, 10000);
});

