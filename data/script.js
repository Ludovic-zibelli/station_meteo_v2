//Infos de la statin
  async function loadStationInfo() {
                try {
                    const res = await fetch('/api/localStationInfo');
                    if (!res.ok) throw new Error('HTTP ' + res.status);
                    const j = await res.json();
                    if (j.error) {
                    document.getElementById('stationVille').textContent = 'Aucune donnée';
                    return;
                    }
                    document.getElementById('stationVille').textContent = j.ville || 'Nom';
                    document.getElementById('stationCP').textContent = j.codepostal || '-';
                    const user = j.user || {};
                    document.getElementById('stationUser').textContent = (user.prenom? (user.prenom+' ') : '') + (user.nom||'-');
                } catch (e) {
                    console.log('Erreur fetch localStationInfo', e);
                    document.getElementById('stationVille').textContent = 'Erreur de chargement';
                }
                }


  window.addEventListener('load', loadStationInfo);
    function escapeHtml(s) {
      if (s === null || s === undefined) return '';
      return String(s).replace(/[&<>"'`]/g, function (m) {
        return ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;','`':'&#96;'})[m];
      });
    }

    
    function setText(id, text) {
      const el = document.getElementById(id);
      if (el) el.textContent = text;
      else console.warn('ID introuvable:', id);
    }


      function rssiToPct(rssi) {
      if (rssi === null || rssi === undefined) return null;
      if (rssi <= -100) return 0;
      if (rssi >= -50) return 100;
      return Math.round((rssi + 100) * 2);
    }
    function pctToBars(pct) {
      if (pct === null) return 0;
      if (pct >= 80) return 4;
      if (pct >= 60) return 3;
      if (pct >= 40) return 2;
      if (pct > 0) return 1;
      return 0;
    }
    function renderWifi(bars, pct, rssi) {
      const container = document.getElementById('wifiSignal');
      const pctEl = document.getElementById('wifiPct');
      const rssiEl = document.getElementById('wifiRssi');
      container.innerHTML = '';
      for (let i = 1; i <= 4; i++) {
        const span = document.createElement('span');
        span.className = 'bar h' + i + ((i <= bars) ? ' on' : '');
        container.appendChild(span);
      }
      pctEl.textContent = (pct !== null) ? (pct + '%') : '--%';
      rssiEl.textContent = (rssi !== null) ? ('RSSI: ' + rssi + ' dBm') : 'RSSI: -';
    }

    
  function updateConnectivityStatus() {
    const el = document.getElementById("connectivity_status");
    if (!el) return;

    let wifiPct = parseInt(document.getElementById("wifiPct").textContent) || 0;

    //  // API status detection
    const rowText = document.getElementById("last_push_row")?.textContent || "";
    const etatText = document.getElementById("last_push_etat")?.textContent || "";

    let apiOK = rowText.includes("200") || etatText.includes("200");

    // Conditions
    if (wifiPct >= 70 && apiOK) {
      el.innerHTML = `<span class="badge badge-status-ok">🟢 Statut : OK</span>`;
    }
    else if (wifiPct >= 30 && apiOK) {
      el.innerHTML = `<span class="badge badge-status-warn">🟡 Statut : Dégradé</span>`;
    }
    else {
      el.innerHTML = `<span class="badge badge-status-ko">🔴 Statut : Hors-ligne</span>`;
    }

  }
  
  
async function chargerDonnees() {
  try {
    const res = await fetch('/data', { headers: { 'Accept': 'application/json' }});
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();

    // Tuiles / valeurs simples
    setText('temp',        (data.temperature != null) ? Number(data.temperature).toFixed(2) : '-');
    setText('hum',         (data.humidite != null) ? Number(data.humidite).toFixed(2) : '-');
    setText('volt',        (data.pression != null) ? Number(data.pression).toFixed(2) : '-');
    setText('temp280',     (data.tempbmp280 != null) ? Number(data.tempbmp280).toFixed(2) : '-');
    setText('datetime',    data.datetime ?? '-');
    setText('tpsvie',      data.tpsvie ?? '-');
    setText('boot_time',   data.boot_time_readable ?? '-');
    setText('pointderosee',(data.pointderosee != null) ? Number(data.pointderosee).toFixed(2) : '-');
    setText('anemometre',  (data.anemometre != null) ? Number(data.anemometre).toFixed(2) : '-');
    setText('rafale',      (data.rafale != null) ? Number(data.rafale).toFixed(2) : '-');
    setText('pluviometre', (data.pluviometre != null) ? Number(data.pluviometre).toFixed(2) : '-');
    // Exemple d’extraction côté JS (data = JSON de /data)
    const fmt = (v, d = 1) => (v === null || Number.isNaN(v)) ? '--' : Number(v).toFixed(d);

    setText("tempMinDay", fmt(data.t_min_day, 1));
    setText("tempMaxDay", fmt(data.t_max_day, 1));
    setText("tempMinAll", fmt(data.t_min_all, 1));
    setText("tempMaxAll", fmt(data.t_max_all, 1));

    setText("humMinDay", fmt(data.h_min_day, 0));
    setText("humMaxDay", fmt(data.h_max_day, 0));
    setText("humMinAll", fmt(data.h_min_all, 0));
    setText("humMaxAll", fmt(data.h_max_all, 0));

    setText("pressMinDay", fmt(data.p_min_day, 1));
    setText("pressMaxDay", fmt(data.p_max_day, 1));
    setText("pressMinAll", fmt(data.p_min_all, 1));
    setText("pressMaxAll", fmt(data.p_max_all, 1));

    setText("vbMinDay", fmt(data.vb_min_day, 2));
    setText("vbMaxDay", fmt(data.vb_max_day, 2));
    setText("vsMinDay", fmt(data.vs_min_day, 2));
    setText("vsMaxDay", fmt(data.vs_max_day, 2));

    setText("gustMaxDay", fmt(data.gust_max_day, 1));
    setText("gustMaxAll", fmt(data.gust_max_all, 1));

    const degToCompass = (deg) => (deg === null || Number.isNaN(deg)) ? '—' : ([
      "N","NNE","NE","ENE","E","ESE","SE","SSE",
      "S","SSW","SW","WSW","W","WNW","NW","NNW"
    ][Math.floor(((deg % 360) / 22.5) + 0.5) % 16]);

    setText("gustDirDay", degToCompass(data.gust_dir_day));
    setText("gustDirAll", degToCompass(data.gust_dir_all));


    // setText('tensionSol',  (data.tension_solaire != null) ? Number(data.tension_solaire).toFixed(2) : '-');
    // setText('tensionBat',  (data.tension_batterie != null) ? Number(data.tension_batterie).toFixed(2) : '-');

    // Wi‑Fi
    let pct = null, rssi = null, bars = 0;
    if ('wifi_percent' in data) pct = data.wifi_percent;
    if ('wifi_rssi'   in data) rssi = data.wifi_rssi;
    if ('wifi_bars'   in data) bars = data.wifi_bars;
    if (pct === null && rssi !== null) pct = rssiToPct(rssi);
    if (pct !== null) bars = pctToBars(pct);
    renderWifi(bars, pct, rssi);
    updateConnectivityStatus();

    // Instruments (Variante B)
    updateInstruments({
      windSpeed:   Number(data.anemometre)   || 0,
      windGust:    Number(data.rafale)       || 0,
      windAvg10:   Number(data.moyenne10min) || 0,
      windDirDeg:  Number(data.direction)    || 0,
      rainDay:     Number(data.pluieJour)    || 0,
      rainHour:    Number(data.pluieHeure)   || 0,
      rainWeek:    Number(data.pluieSemaine) || 0,
      windScaleMax: 50,
      windStatus:  'OK'
    });

  } catch (e) {
    console.log('Erreur fetch /data:', e);
  }
}


        function renderPush(elementId, dataPrefix, data) {
      const el = document.getElementById(elementId);
      if (!el || !data) return;
      const code = data[dataPrefix + '_code'];
      const body = data[dataPrefix + '_body'] || '';
      const ageMs = data[dataPrefix + '_age_ms'];
      const endpoint = data[dataPrefix + '_endpoint'] || '';

      const ageS = (typeof ageMs === 'number' && !isNaN(ageMs)) ? Math.round(ageMs/1000) + 's' : '';
      let badgeClass = 'badge bg-secondary';
      if (typeof code === 'number') {
        if (code >= 200 && code < 300) badgeClass = 'badge bg-success';
        else if (code < 0) badgeClass = 'badge bg-danger';
        else badgeClass = 'badge bg-warning text-dark';
      }

      const codeHtml = (code !== undefined) ? ('<span class="' + badgeClass + '">' + escapeHtml(code) + '</span>') : '<span class="badge bg-secondary">—</span>';
      const epHtml = endpoint ? (' <small class="text-muted">' + escapeHtml(endpoint) + '</small>') : '';
      const ageHtml = ageS ? (' <small class="text-muted">· ' + escapeHtml(ageS) + '</small>') : '';

      el.innerHTML = codeHtml + epHtml + ageHtml +
                     (body ? ('<div class="push-body text-truncate">' + escapeHtml(body) + '</div>') : '');

    el.innerHTML = codeHtml + epHtml + ageHtml +
                   (body ? ('<div class="push-body text-truncate">' + escapeHtml(body) + '</div>') : '');
  }



      async function fetchPushStatus() {
      try {
        const res = await fetch('/api/localStationInfo', {cache: 'no-store'});
        if (!res.ok) {
          console.warn('fetch push status http', res.status);
          return;
        }
        const data = await res.json();
        // met à jour uniquement l'affichage des pushes
        renderPush('last_push_row', 'last_push_row', data);
        renderPush('last_push_etat', 'last_push_etat', data);
        updateConnectivityStatus();
      } catch (err) {
        console.error('fetchPushStatus error', err);
      }
    }

  //adc

  function fmt(v){ return (isNaN(v)?'—':(+v).toFixed(2)); }
  function clamp(v,min,max){ return Math.max(min, Math.min(max, v)); }

  // Ajuste les bornes de visualisation (adapter à ton cas réel)
  const MAX_SOLAR = 9.5;   // tension max solaire attendue (côté source)
  const MAX_BATT  = 4.2;    // tension max batterie

  async function poll(){
    try{
      const r = await fetch('/adc/live',{cache:'no-store'});
      if(!r.ok) throw new Error('HTTP '+r.status);
      const j = await r.json();

      // Batterie
      const vBatt = j.batt?.v_corr ?? NaN;
      const vAdcBatt = j.batt?.v_adc ?? NaN;
      const rawBatt  = j.batt?.raw ?? NaN;
      document.getElementById('v_batt').textContent = fmt(vBatt)+' V';
      document.getElementById('v_adc_batt').textContent = fmt(vAdcBatt);
      document.getElementById('raw_batt').textContent = rawBatt;
      const pctBatt = clamp((vBatt / MAX_BATT) * 100, 0, 100);
      const barBatt = document.getElementById('bar_batt');
      barBatt.style.width = pctBatt.toFixed(0)+'%';
      barBatt.textContent = pctBatt.toFixed(0)+'%';

      // Solaire
      const vSolar = j.solar?.v_corr ?? NaN;
      const vAdcSolar = j.solar?.v_adc ?? NaN;
      const rawSolar  = j.solar?.raw ?? NaN;
      document.getElementById('v_solar').textContent = fmt(vSolar)+' V';
      document.getElementById('v_adc_solar').textContent = fmt(vAdcSolar);
      document.getElementById('raw_solar').textContent = rawSolar;
      const pctSolar = clamp((vSolar / MAX_SOLAR) * 100, 0, 100);
      const barSolar = document.getElementById('bar_solar');
      barSolar.style.width = pctSolar.toFixed(0)+'%';
      barSolar.textContent = pctSolar.toFixed(0)+'%';


    }catch(e){
      // En cas d’OTA ou d’erreur réseau, on ne casse pas l’UI
      console.debug('poll error', e);
    }finally{
      setTimeout(poll, 500);
    }
  }
  
  
document.querySelectorAll('#v_batt').length
document.querySelectorAll('#v_solar').length


// — Helpers — //
function degToCardinal(d){
  const dirs=['N','NNE','NE','ENE','E','ESE','SE','SSE','S','SSW','SW','WSW','W','WNW','NW','NNW'];
  return dirs[Math.round(((d%360)+360)%360/22.5)%16];
}
// mappe [0..max] -> arc demi-cercle SVG
function arcPath(cx, cy, r, v, vmax){
  const clamped = Math.max(0, Math.min(v, vmax));
  const a = (clamped / vmax) * Math.PI; // 0..π
  const start = { x: cx - r, y: cy };
  const end   = { x: cx - r*Math.cos(a), y: cy - r*Math.sin(a) };
  return `M ${start.x},${start.y} A ${r},${r} 0 0 1 ${end.x},${end.y}`;
}
function setBadgeClass(el, status){
  el.classList.remove('ok','warn','bad');
  const s = (status||'').toLowerCase();
  if (s.includes('ok') || s.includes('bon') || s.includes('normal')) el.classList.add('ok');
  else if (s.includes('avert') || s.includes('faible') || s.includes('moyen')) el.classList.add('warn');
  else el.classList.add('bad');
}

// — API simple pour mettre à jour l’affichage — //
function updateInstruments({ windSpeed, windGust, windAvg10, windDirDeg, rainDay, rainHour, rainWeek, windScaleMax=50, windStatus='OK' }){
  // Anémomètre
  const path = document.getElementById('gaugeWindVal');
  const txt  = document.getElementById('gaugeWindTxt');
  if (path && txt){
    path.setAttribute('d', arcPath(80,80,70, windSpeed||0, windScaleMax));
    txt.textContent = (Number(windSpeed)||0).toFixed(1);
  }
  const gust = document.getElementById('gGust');
  const avg  = document.getElementById('gAvg');
  if (gust) gust.textContent = (Number(windGust)||0).toFixed(1);
  if (avg)  avg.textContent  = (Number(windAvg10)||0).toFixed(1);
  const qual = document.getElementById('windQual');
  if (qual){ qual.textContent = windStatus; setBadgeClass(qual, windStatus); }

  // Girouette
  const needle = document.getElementById('needle');
  const degEl  = document.getElementById('dirDeg');
  const cardEl = document.getElementById('dirCard');
  const deg = Number(windDirDeg)||0;
  if (needle) needle.style.transform = `translate(-50%,-100%) rotate(${deg}deg)`;
  if (degEl)  degEl.textContent = `${Math.round(deg)}°`;
  if (cardEl) cardEl.textContent = degToCardinal(deg);

  // Pluviomètre
  const d = document.getElementById('rainDayB');
  const h = document.getElementById('rainHour');
  const w = document.getElementById('rainWeekB');
  if (d) d.textContent = (Number(rainDay)||0).toFixed(2);
  if (h) h.textContent = (Number(rainHour)||0).toFixed(1);
  if (w) w.textContent = (Number(rainWeek)||0).toFixed(1);
}

// — Exemple d’appel (à supprimer et remplacer par tes vraies valeurs) —
// updateInstruments({
//   windSpeed: 12.6, windGust: 22.3, windAvg10: 9.8,
//   windDirDeg: 248,
//   rainDay: 1.80, rainHour: 0.2, rainWeek: 6.4,
//   windScaleMax: 50, windStatus: 'OK'
// });



