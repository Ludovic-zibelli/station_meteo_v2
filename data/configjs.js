
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

// ============== Version info ==============
async function loadVersionInfo() {
  try {
    const ver = await getJSON('/api/version');
    setText('fw_version', ver.firmware || '—');
    setText('config_version', ver.config_schema || '—');
    setText('build_date', ver.build_date || '—');
  } catch(err) {
    console.warn('loadVersionInfo error:', err);
  }

  try {
    const info = await getJSON('/api/localStationInfo');
    if (info.config_last_modified) {
      const dt = new Date(info.config_last_modified * 1000);
      setText('config_modified', dt.toLocaleString('fr-FR'));
    }
  } catch(err) {
    console.warn('config_last_modified fetch error:', err);
  }
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

  loadVersionInfo();  // Charger les infos de version en premier
  loadConfig();
  refreshLive();
  setInterval(refreshLive, 10000);
});



// ====== LOG (/log.txt) ======

// Helper texte brut (no-cache)
async function getText(url) {
  const res = await fetch(url, { cache: 'no-store' });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.text();
}

// Timer d'auto-refresh
let logTimer = null;

// Chargement des logs (N dernières lignes depuis /log/tail)
async function loadLog(manual = false) {
  try {
    const nInput = $('logLines');
    const n = Math.max(50, Math.min(2000, Number(nInput ? nInput.value : 250)));

    // Lecture directe de la tail côté ESP (léger et fiable)
    let raw = await getText(`/log/tail?n=${n}`);

    // Normalisation douce (si jamais il y a des CR ou NUL)
    raw = raw.replace(/\0/g, '').replace(/\r\n/g, '\n').replace(/\r/g, '\n');

    const box = $('logTail');
    if (box) {
      box.textContent = raw && raw.trim().length ? raw : '(journal vide)';
      const auto = $('logAuto');
      if (auto && auto.checked) box.scrollTop = box.scrollHeight;
    }

    setText('logStatus', `OK — ${n} lignes affichées • ${new Date().toLocaleTimeString()}`);
    if (manual) toast('Journal rafraîchi ✅');

  } catch (e) {
    console.error('loadLog error:', e);
    setText('logStatus', `Erreur: ${e.message}`);
    if (manual) toast(`Erreur lecture journal: ${e.message}`, false);
  }
}

// Effacement du log
async function clearLog() {
  try {
    const res = await fetch('/log/clear', { method: 'POST' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    toast('Journal vidé ✅');
    await loadLog(true);
  } catch (e) {
    console.error('clearLog error:', e);
    toast(`Erreur effacement: ${e.message}`, false);
  }
}

// (Re)démarrage de l’auto-refresh
function startLogAuto() {
  if (logTimer) clearInterval(logTimer);
  const auto = $('logAuto');
  if (auto && auto.checked) {
    logTimer = setInterval(loadLog, 5000);
  }
}

// Branche les événements du panneau “Journal”
document.addEventListener('DOMContentLoaded', () => {
  on('btnLogRefresh', 'click', () => loadLog(true));
  on('btnLogClear', 'click', clearLog);

  // Ces deux-là sont optionnels si tu as gardé des <a> href="/log.txt" dans config.html
  on('btnLogOpen', 'click', () => window.open('/log.txt', '_blank'));
  on('btnLogDownload', 'click', () => window.open('/log.txt', '_blank'));

  on('logAuto', 'change', startLogAuto);
  on('logLines', 'change', () => loadLog(true));

  // Premier affichage + auto-refresh
  loadLog(false);
  startLogAuto();
});

// ====== Authentification & AP ======
document.getElementById('btnLoadAuth').onclick = async ()=>{
  const r = await fetch('/config/auth'); if(!r.ok) return alert('Erreur');
  const j = await r.json();
  viewer_user.value = j.auth.viewer_user;
  admin_user.value  = j.auth.admin_user;
  ap_ssid.value     = j.ap.ssid;
  ap_chan.value     = j.ap.chan;
  ap_maxc.value     = j.ap.maxc;
  // (Les mots de passe sont masqués côté serveur)
};

document.getElementById('btnSaveAuth').onclick = async ()=>{
  const fd = new FormData();
  if (viewer_user.value) fd.append('viewer_user', viewer_user.value);
  if (viewer_pass.value) fd.append('viewer_pass', viewer_pass.value);
  if (admin_user.value)  fd.append('admin_user', admin_user.value);
  if (admin_pass.value)  fd.append('admin_pass', admin_pass.value);
  if (ap_ssid.value)     fd.append('ap_ssid', ap_ssid.value);
  if (ap_pass.value)     fd.append('ap_pass', ap_pass.value);
  if (ap_chan.value)     fd.append('ap_chan', ap_chan.value);
  if (ap_maxc.value)     fd.append('ap_maxc', ap_maxc.value);

  const r = await fetch('/config/auth.save', { method:'POST', body:fd });
  if (r.ok) alert('OK (AP redémarré si modifié).');
  else alert('Erreur sauvegarde');
};


const ipMode = document.getElementById('sta_ipmode'), ipInput = document.getElementById('sta_ip');
ipMode.onchange = ()=>{ ipInput.style.display = (ipMode.value === 'static') ? 'inline-block' : 'none'; };

document.getElementById('btnWifiScan').onclick = async ()=>{
  const r = await fetch('/wifi/scan.json');   // protégé admin côté serveur
  if (!r.ok) { alert("Scan en cours, réessaie dans 2-3s…"); return; }
  const j = await r.json();
  const body = document.getElementById('scanBody'); body.innerHTML = '';
  (j.nets||[]).forEach(n=>{
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${n.ssid}</td><td>${n.rssi} dBm</td><td>${n.chan}</td>
      <td><button class="btn btn-outline" data-ssid="${n.ssid}">Choisir</button></td>`;
    body.appendChild(tr);
  });
  document.getElementById('scanResults').style.display = 'block';
  body.querySelectorAll('button[data-ssid]').forEach(b=>{
    b.onclick = ()=>{ sta_ssid.value = b.dataset.ssid; };
  });
};

document.getElementById('btnWifiSave').onclick = async ()=>{
  const fd = new FormData();
  if (!sta_ssid.value) { alert("Indique le SSID du Wi‑Fi maison."); return; }
  fd.append('ssid', sta_ssid.value);
  if (sta_pass.value)  fd.append('wifi_password', sta_pass.value);
  fd.append('ip_wifi', ipMode.value === 'static' ? (ipInput.value||'dhcp') : 'dhcp');

  const r = await fetch('/config/network', { method:'POST', body:fd });
  if (r.ok) alert('OK. La station tente de se reconnecter au Wi‑Fi maison.');
  else      alert('Erreur : ' + await r.text());
};


document.getElementById('btnLoadOfs').onclick = async ()=>{
  const r = await fetch('/config/offsets');
  if (!r.ok) return alert('Erreur lecture offsets');
  const j = await r.json();
  ofs_t_bmp.value   = j.t_bmp ?? 0;
  ofs_t_dht.value   = j.t_dht ?? 0;
  ofs_h_dht.value   = j.h_dht ?? 0;
  ofs_press.value   = j.press ?? 0;
  ofs_wind.value    = j.wind ?? 0;
  ofs_t_sht40.value = j.t_sht40 ?? 0;
};
document.getElementById('btnSaveOfs').onclick = async ()=>{
  const fd = new FormData();
  fd.append('t_bmp',   ofs_t_bmp.value || 0);
  fd.append('t_dht',   ofs_t_dht.value || 0);
  fd.append('h_dht',   ofs_h_dht.value || 0);
  fd.append('press',   ofs_press.value || 0);
  fd.append('wind',    ofs_wind.value || 0);
  fd.append('t_sht40', ofs_t_sht40.value || 0);
  const r = await fetch('/config/offsets.save', { method:'POST', body:fd });
  if (!r.ok) return alert('Erreur sauvegarde');
  alert('Offsets enregistrés.');
};

document.getElementById('btnResetRec').onclick = async ()=>{
  if (!confirm('Confirmer la réinitialisation des min/max ?')) return;
  const fd = new FormData(); fd.append('scope', resetScope.value);
  const r = await fetch('/records/reset', { method:'POST', body:fd });
  if (!r.ok) return alert('Erreur reset');
  alert('Min/Max réinitialisés.');
};


// Bouton "Relancer BMP280"
on('btnBmpReinit', 'click', async ()=>{
  try {
    const r = await fetch('/sensor/bmp280/reinit', { cache: 'no-store' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const j = await r.json();
    toast(j.ok ? 'BMP280 réinitialisé ✅' : 'Réinitialisation BMP280 : échec', !!j.ok);
  } catch (e) {
    console.error('bmp reinit error:', e);
    toast('Erreur réinit BMP280: ' + e.message, false);
  }
});


on('btnBmpReinit', 'click', async ()=>{
  try {
    const r = await fetch('/sensor/bmp280/reinit', { cache: 'no-store' });
    const j = r.ok ? await r.json() : { ok:false };
    toast(j.ok ? 'BMP280 réinitialisé ✅' : 'Réinitialisation BMP280 : échec', !!j.ok);
    if (j.ok) { try { await loadConfig(); } catch(_){} }  // ← recharge les états
  } catch (e) {
    toast('Erreur réinit BMP280: ' + e.message, false);
  }
});



async function jget(url)  { const r = await fetch(url,  {cache:'no-store'}); if(!r.ok) throw new Error(r.status); return r.json(); }
async function jpost(url, data) {
  const r = await fetch(url, {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body: new URLSearchParams(data)});
  if(!r.ok) throw new Error(r.status);
  return r.headers.get('content-type')?.includes('json') ? r.json() : r.text();
}

async function refreshBmpDiag(){
  const s = await jget('/status.json');   // conf/state
  const d = await jget('/data');          // live avec bmp_status/bmp_bad_streak

  document.getElementById('bmp_addr').textContent = s.conf?.bmp280_addr ?? '—';

  const status = d.bmp_status ?? null;
  const badge  = document.getElementById('bmp_status_badge');
  const map = {
    1:  ['OK','bg-success'],
    [-1]:['NAN','bg-warning'],
    [-2]:['ID','bg-danger'],
    [-3]:['BUS','bg-danger'],
    [-4]:['RESET','bg-info'],
  };
  const ent = map[status] ?? ['?','bg-secondary'];
  badge.className = 'badge ' + ent[1];
  badge.textContent = ent[0];

  document.getElementById('bmp_streak').textContent = d.bmp_bad_streak ?? 0;

  // offsets
  const o = await jget('/config/offsets');
  document.getElementById('ofs_t_bmp').value = (o.t_bmp ?? 0).toFixed(1);
  document.getElementById('ofs_press').value = (o.press ?? 0).toFixed(1);
}

document.getElementById('frm_offsets').addEventListener('submit', async (e)=>{
  e.preventDefault();
  const t_bmp = document.getElementById('ofs_t_bmp').value;
  const press = document.getElementById('ofs_press').value;
  await jpost('/config/offsets.save', { t_bmp, press });
  alert('Offsets enregistrés');
  refreshBmpDiag();
});

document.getElementById('btn_bmp_softreset').addEventListener('click', async ()=>{
  try {
    await fetch('/sensor/bmp280/softreset', { method:'POST' });
    setTimeout(refreshBmpDiag, 1500); // le temps que le soft-reset s’applique
  } catch(e) {
    alert('Soft reset échec: ' + e);
  }
});

// initial & refresh
refreshBmpDiag();
setInterval(refreshBmpDiag, 5000);
