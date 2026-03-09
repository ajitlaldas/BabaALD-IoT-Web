import { initializeApp } from "https://www.gstatic.com/firebasejs/10.8.0/firebase-app.js";
import { getAuth, onAuthStateChanged, signOut } from "https://www.gstatic.com/firebasejs/10.8.0/firebase-auth.js";
import { getDatabase, ref, onValue, update, set, remove } from "https://www.gstatic.com/firebasejs/10.8.0/firebase-database.js";

const firebaseConfig = {
    apiKey: "AIzaSyCzWlIyMppyUBIeJVbDbWfu-ztohrxaTEo",
    authDomain: "esp32-babaald.firebaseapp.com",
    databaseURL: "https://esp32-babaald-default-rtdb.firebaseio.com",
    projectId: "esp32-babaald",
    storageBucket: "esp32-babaald.firebasestorage.app",
    messagingSenderId: "911018871293",
    appId: "1:911018871293:web:5e137fc1ca18b4c6a4b194"
};

const app  = initializeApp(firebaseConfig);
const auth = getAuth(app);
const db   = getDatabase(app);

let currentOtp     = null;
let currentDevices = {};
let expandedCard   = null;
const MAX_DEVICES  = 6;

const deviceMaster = {
    "Smart Lamp":                ["OnOff","SetColour","SetPattern","NightMode"],
    "Smart Fan":                 ["OnOff","SpeedControl","Set TimerBased OnOff","Temp Based OnOff","Set Auto OnOff Time"],
    "Smart Speaker":             ["OnOff","Set Volume","Set Internet Radio Mode","Set TimerBased OnOff"],
    "Smart Lamp with Alarm":     ["OnOff","SetColour","SetPattern","NightMode","Set Volume","Set TimerBased OnOff","Set Alarm"],
    "Smart Cooktop":             ["OnOff","Power Control","Set Timer Based OnOff","Temp Based OnOff","Set Auto OnOff Time"],
    "Smart Fridge":              ["OnOff","Set Timer Based OnOff","Set Auto OnOff Time"],
    "Smart Intrusion Detection": ["OnOff","Set Sensitivity","Set email Notification","Set SMS Notification"]
};

const attrTooltips = {
    "OnOff":                   "Switch ON or OFF the Device",
    "SetColour":               "Set Lamp Colour Here",
    "SetPattern":              "Set Lamp Glow Pattern here. If set to default it will glow as per selected colour combination",
    "NightMode":               "Put device in Night Mode. It will glow faint in calm warm colour",
    "SpeedControl":            "Set your Smart Device's (like Fan) Speed",
    "Set TimerBased OnOff":    "Set to Switch ON the device and Switch off after the time duration you selected",
    "Set Timer Based OnOff":   "Set to Switch ON the device and Switch off after the time duration you selected",
    "Temp Based OnOff":        "Set This to automatically Switch ON or OFF Device based on temp (for cooktop this will trigger only once based on Temperature of food item)",
    "Set Auto OnOff Time":     "Set This to automatically Switch ON or OFF Device based on time of the day",
    "Set Volume":              "Set Device Volume (for alarm and speaker both)",
    "Set Internet Radio Mode": "Put Speaker in Internet Radio Mode",
    "Set Alarm":               "Set This to sound alarm",
    "Power Control":           "Set Device Power (for cooktops)",
    "Set Sensitivity":         "Set Sensitivity for intrusion detection (100 means most sensitive)",
    "Set email Notification":  "Set This for email alert on intrusion",
    "Set SMS Notification":    "Set This for SMS alert on intrusion"
};

const colourPresets = {
    "RGB Mix":     null,
    "Warm White":  [255,244,229],
    "Scarlet Red": [255,36,0],
    "Light Blue":  [135,206,235],
    "Baby Pink":   [255,182,193],
    "Orange":      [255,140,0],
    "Violet":      [138,43,226],
    "Green":       [0,200,83]
};

// ── Auth ──────────────────────────────────────────────────────
onAuthStateChanged(auth, function(user) {
    if (user) {
        var g = document.getElementById('user-greeting');
        if (g) g.textContent = 'Hi, ' + (user.displayName || user.email || 'Welcome back!');
        monitorDevices(user.uid);
    } else {
        window.location.href = "index.html";
    }
});

// ── Real-time listener ────────────────────────────────────────
function monitorDevices(uid) {
    onValue(ref(db, 'users/' + uid + '/devices'), function(snapshot) {
        currentDevices = snapshot.val() || {};
        renderDeviceGrid(uid);
        populateManageSelect();
    });
}

// ── Device Grid ───────────────────────────────────────────────
function renderDeviceGrid(uid) {
    var grid = document.getElementById('dynamic-device-grid');
    if (!grid) return;
    grid.innerHTML = '';

    var names = Object.keys(currentDevices).sort();
    if (names.length === 0) {
        grid.innerHTML = '<p style="color:var(--muted);font-size:0.95rem;grid-column:1/-1;">No devices yet. Add one from the sidebar!</p>';
        return;
    }

    if (expandedCard && !currentDevices[expandedCard]) expandedCard = null;

    names.forEach(function(name) {
        var dev    = currentDevices[name];
        var safeId = sanitizeId(name);
        var isExp  = (name === expandedCard);

        var card = document.createElement('div');
        card.className    = 'device-card ' + (isExp ? 'expanded' : 'condensed');
        card.dataset.name = name;

        card.innerHTML =
            '<div class="card-header" onclick="window.toggleCard(\'' + safeId + '\')">' +
                '<div class="card-title-group">' +
                    '<h4 class="card-device-name">' + escapeHtml(name) + '</h4>' +
                    '<span class="device-type-badge">' + escapeHtml(dev.type || '') + '</span>' +
                        '<span class="last-seen-label">&#128261; Last Successful Handshake: <span id="lastsucessfulhandshake-' + safeId + '">' + formatLastSeen(dev) + '</span></span>' +
                '</div>' +
                '<span class="expand-icon">' + (isExp ? '&#9650;' : '&#9660;') + '</span>' +
            '</div>' +
            '<div class="card-condensed-row" id="condensed-' + safeId + '"></div>' +
            '<div class="card-expanded-body' + (isExp ? '' : ' hidden') + '" id="expanded-' + safeId + '"></div>';

        grid.appendChild(card);
        renderCondensed(safeId, dev, uid);
        renderControls(safeId, dev, uid);
    });

    applyExpandedLayout();
}

// ── safeId <-> name lookup ────────────────────────────────────
function getNameBySafeId(safeId) {
    var names = Object.keys(currentDevices);
    for (var i = 0; i < names.length; i++) {
        if (sanitizeId(names[i]) === safeId) return names[i];
    }
    return safeId;
}

function formatLastSeen(dev) {
    // Prefer explicit date+time fields when present
    var d = dev && dev.lastSeenDate;
    var t = dev && dev.lastSeenTime;
    if (d || t) {
        if (d && t) return d + ' (' + t + ')';
        return d || t;
    }

    // If device writes explicit last-successful-handshake date/time fields, prefer them
    var d2 = dev && (dev.lastSucessfulHandshakeDate || dev.lastSuccessfulHandshakeDate || dev.lastSeenDate);
    var t2 = dev && (dev.lastSucessfulHandshakeTime || dev.lastSuccessfulHandshakeTime || dev.lastSeenTime);
    if (d2 || t2) {
        if (d2 && t2) return d2 + ' (' + t2 + ')';
        return d2 || t2;
    }

    // If device writes a Unix epoch timestamp (seconds or milliseconds), accept it
    var epoch = dev && (dev.lastSucessfulHandshakeEpoch || dev.lastSuccessfulHandshakeEpoch || dev.lastSeenEpoch || dev.lastSeen || dev.last_seen || dev.lastSeenAt || dev.lastSeenTimestamp);
    if (epoch) {
        var s = formatEpochToDisplay(epoch);
        if (s) return s;
    }

    return 'Not Yet Live';
}

// Convert an epoch (seconds or milliseconds) into "HH:MM AM/PM DD/MM/YYYY" if valid
function formatEpochToDisplay(epoch) {
    if (epoch === null || epoch === undefined) return null;
    var n = Number(epoch);
    if (!isFinite(n) || n <= 0) return null;
    // If epoch looks like seconds (less than 1e12), convert to ms
    if (n < 1e12) n = n * 1000;
    // Minimum allowed timestamp: 2021-01-01 UTC
    var MIN_TS = 1609459200000;
    if (n < MIN_TS) return null;
    var dt = new Date(n);
    if (isNaN(dt.getTime())) return null;

    var hh = dt.getHours();
    var ampm = hh >= 12 ? 'PM' : 'AM';
    var hrs = hh % 12; if (hrs === 0) hrs = 12;
    var mins = dt.getMinutes();
    var dd = dt.getDate();
    var mm = dt.getMonth() + 1;
    var yyyy = dt.getFullYear();
    function pad(x){ return (x < 10 ? '0' : '') + x; }
    return hrs + ':' + pad(mins) + ' ' + ampm + ' ' + pad(dd) + '/' + pad(mm) + '/' + yyyy;
}

function applyExpandedLayout() {
    var grid = document.getElementById('dynamic-device-grid');
    if (!grid) return;
    var all    = Array.from(grid.querySelectorAll('.device-card'));
    var expEl  = all.find(function(c){ return c.dataset.name === expandedCard; });
    if (!expEl) {
        grid.classList.remove('has-expanded');
        all.forEach(function(c){ c.classList.remove('card-right-stack'); });
    } else {
        grid.classList.add('has-expanded');
        all.forEach(function(c){ c.classList.toggle('card-right-stack', c !== expEl); });
    }
}

// toggleCard now uses safeId to avoid spaces in onclick attribute
window.toggleCard = function(safeId) {
    var name = getNameBySafeId(safeId);
    expandedCard = (expandedCard === name) ? null : name;
    var uid = getCurrentUid();
    if (uid) renderDeviceGrid(uid);
};

// ── Condensed row ─────────────────────────────────────────────
function renderCondensed(safeId, dev, uid) {
    var container = document.getElementById('condensed-' + safeId);
    if (!container) return;
    var attrs = dev.attributes || {};
    var name  = getNameBySafeId(safeId);
    if (!attrs['OnOff']) { container.innerHTML = ''; return; }
    container.innerHTML =
        '<div class="condensed-toggle" title="' + attrTooltips['OnOff'] + '">' +
            '<span class="condensed-label">Switch On/Off</span>' +
            '<label class="toggle-switch">' +
                '<input type="checkbox" onchange="window.sf(\'' + safeId + '\',\'isON\',this.checked)"' + (dev.isON ? ' checked' : '') + '>' +
                '<span class="toggle-track"><span class="toggle-thumb"></span></span>' +
            '</label>' +
        '</div>';
}

// ── Column layout helper ──────────────────────────────────────
function getColumnLayout(attrs) {
    var hasColour = !!attrs['SetColour'];
    var count     = Object.keys(attrs).filter(function(k){ return k !== 'OnOff'; }).length;
    if (hasColour && count >= 4) return 'colour-split';
    if (count >= 5)              return 'two-col';
    return 'one-col';
}

// ── Full control renderer ─────────────────────────────────────
function renderControls(safeId, data, uid) {
    var container = document.getElementById('expanded-' + safeId);
    if (!container) return;
    var attrs = data.attributes || {};

    function wrap(key, content) {
        var tip = escapeHtml(attrTooltips[key] || '');
        return '<div class="attr-section" title="' + tip + '">' + content + '</div><div class="attr-band-divider"></div>';
    }

    function tog(label, field, value, key) {
        return wrap(key,
            '<div class="attr-row-toggle">' +
                '<span class="attr-pill-label">' + label + '</span>' +
                '<label class="toggle-switch">' +
                    '<input type="checkbox" onchange="window.sf(\'' + safeId + '\',\'' + field + '\',this.checked)"' + (value ? ' checked' : '') + '>' +
                    '<span class="toggle-track"><span class="toggle-thumb"></span></span>' +
                '</label>' +
            '</div>'
        );
    }

    // ── Build HTML blocks ─────────────────────────────────────
    var colourHtml = '';
    var blocks = [];

    // RGB Colour
    if (attrs['SetColour']) {
        var presetOpts = Object.keys(colourPresets).map(function(p){
            return '<option value="' + p + '">' + p + '</option>';
        }).join('');
        colourHtml =
            '<div class="attr-section colour-section" title="' + escapeHtml(attrTooltips['SetColour']) + '">' +
                '<span class="attr-pill-label">RGB Color Mixer</span>' +
                '<div class="rgb-knob-row" style="display:flex;align-items:center;gap:14px;">' +
                    '<div class="rgb-knob-group" style="display:flex;gap:12px;align-items:center;">' +
                        '<div class="knob-col">' +
                            '<input type="text" class="kr-' + safeId + '" value="' + (data.RED||0) + '" data-min="0" data-max="255" data-fgColor="#e74c3c" data-width="98" data-height="98">' +
                            '<span class="knob-lbl" style="color:#e74c3c;">R</span>' +
                        '</div>' +
                        '<div class="knob-col">' +
                            '<input type="text" class="kg-' + safeId + '" value="' + (data.GREEN||0) + '" data-min="0" data-max="255" data-fgColor="#2ecc71" data-width="98" data-height="98">' +
                            '<span class="knob-lbl" style="color:#2ecc71;">G</span>' +
                        '</div>' +
                        '<div class="knob-col">' +
                            '<input type="text" class="kb-' + safeId + '" value="' + (data.BLUE||0) + '" data-min="0" data-max="255" data-fgColor="#3498db" data-width="98" data-height="98">' +
                            '<span class="knob-lbl" style="color:#3498db;">B</span>' +
                        '</div>' +
                    '</div>' +
                    '<div class="rgb-vertical-divider" aria-hidden="true" style="width:1px;height:86px;background:rgba(0,0,0,0.08);"></div>' +
                    '<div class="brightness-col" title="Adjust lamp brightness here" style="border:2px solid #f1c40f;border-radius:8px;padding:6px;display:flex;align-items:center;justify-content:center;">' +
                        '<div style="text-align:center;">' +
                            '<input type="text" class="kbright-' + safeId + '" value="' + (data.BRIGHTNESS ? Math.round((data.BRIGHTNESS/255)*100) : 50) + '" data-min="0" data-max="100" data-fgColor="#f1c40f" data-width="98" data-height="98">' +
                            '<div class="knob-lbl" style="color:#f1c40f;margin-top:6px;font-weight:700;letter-spacing:0.6px;">BRIGHTNESS</div>' +
                        '</div>' +
                    '</div>' +
                    '<div class="rgb-preview-circle" id="preview-' + safeId + '" style="background:rgb(' + (data.RED||0) + ',' + (data.GREEN||0) + ',' + (data.BLUE||0) + ')"></div>' +
                '</div>' +
                '<div class="colour-preset-row">' +
                    '<label class="mini-label">Preset</label>' +
                    '<select class="styled-select-inline" id="preset-' + safeId + '" onchange="window.applyPreset(\'' + safeId + '\',this.value)">' + presetOpts + '</select>' +
                '</div>' +
                '<button class="fancy-btn btn-navy" style="margin-top:10px;" onclick="window.applyRGB(\'' + safeId + '\')">Set Color</button>' +
            '</div><div class="attr-band-divider"></div>';
    }

    // Pattern
    if (attrs['SetPattern']) {
        var pats = ['default','random','heartbeat','groove','psychedelics','mutate','rotate'];
        var patOpts = pats.map(function(p){
            return '<option value="' + p + '"' + ((data.Pattern||'default')===p?' selected':'') + '>' + p.charAt(0).toUpperCase()+p.slice(1) + '</option>';
        }).join('');
        blocks.push(wrap('SetPattern',
            '<span class="attr-pill-label">Lighting Pattern</span>' +
            '<select class="styled-select-card" onchange="window.sf(\'' + safeId + '\',\'Pattern\',this.value)">' + patOpts + '</select>'
        ));
    }

    // Knobs
    function knob(key, label, cls, field, val, mn, mx, col) {
        return wrap(key,
            '<span class="attr-pill-label">' + label + '</span>' +
            '<div style="text-align:center;margin-top:8px;">' +
                '<input type="text" class="' + cls + '-' + safeId + '" value="' + val + '" data-min="' + mn + '" data-max="' + mx + '" data-fgColor="' + col + '" data-width="90" data-height="90" data-angleoffset="-125" data-anglearc="250">' +
            '</div>'
        );
    }
    if (attrs['SpeedControl'])    blocks.push(knob('SpeedControl',   'Fan Speed',   'kspeed','Speed',       data.Speed      ||1,  1,  5,  '#1a2a6c'));
    if (attrs['Set Volume'])      blocks.push(knob('Set Volume',     'Volume',      'kvol',  'Volume',      data.Volume     ||50, 0, 100, '#27ae60'));
    if (attrs['Power Control'])   blocks.push(knob('Power Control',  'Power Level', 'kpower','PowerLevel',  data.PowerLevel ||1,  1,  10, '#f24e1e'));
    if (attrs['Set Sensitivity']) blocks.push(knob('Set Sensitivity','Sensitivity', 'ksense','Sensitivity', data.Sensitivity||50, 0, 100, '#b21f1f'));

    // Temp
    if (attrs['Temp Based OnOff']) {
        blocks.push(wrap('Temp Based OnOff',
            '<span class="attr-pill-label">Temp Control (°C)</span>' +
            '<div class="input-row">' +
                '<input type="number" id="tOn-' + safeId + '" class="card-input" value="' + (data.OnSetPoint||25) + '" placeholder="ON °C">' +
                '<input type="number" id="tOff-' + safeId + '" class="card-input" value="' + (data.OffSetPoint||20) + '" placeholder="OFF °C">' +
                '<button class="inline-save-btn" onclick="window.saveTemp(\'' + safeId + '\')">Save</button>' +
            '</div>' +
            '<div class="attr-row-toggle" style="margin-top:10px;">' +
                '<span style="font-size:0.85rem;color:var(--muted);">Enable</span>' +
                '<label class="toggle-switch"><input type="checkbox" onchange="window.sf(\'' + safeId + '\',\'isTempEnabled\',this.checked)"' + (data.isTempEnabled?' checked':'') + '><span class="toggle-track"><span class="toggle-thumb"></span></span></label>' +
            '</div>'
        ));
    }

    // Timer
    if (attrs['Set TimerBased OnOff'] || attrs['Set Timer Based OnOff']) {
        var tKey = attrs['Set TimerBased OnOff'] ? 'Set TimerBased OnOff' : 'Set Timer Based OnOff';
        blocks.push(wrap(tKey,
            '<span class="attr-pill-label">Timer (Mins)</span>' +
            '<div class="input-row">' +
                '<input type="number" id="timer-' + safeId + '" class="card-input" value="' + (data.TimerMins||10) + '" placeholder="Mins">' +
                '<button class="inline-save-btn" onclick="window.sf(\'' + safeId + '\',\'TimerMins\',document.getElementById(\'timer-' + safeId + '\').value)">Save</button>' +
            '</div>' +
            '<div class="attr-row-toggle" style="margin-top:10px;">' +
                '<span style="font-size:0.85rem;color:var(--muted);">Enable</span>' +
                '<label class="toggle-switch"><input type="checkbox" onchange="window.sf(\'' + safeId + '\',\'isTimerEnabled\',this.checked)"' + (data.isTimerEnabled?' checked':'') + '><span class="toggle-track"><span class="toggle-thumb"></span></span></label>' +
            '</div>'
        ));
    }

    // Schedule
    if (attrs['Set Auto OnOff Time']) {
        blocks.push(wrap('Set Auto OnOff Time',
            '<span class="attr-pill-label">Auto Schedule</span>' +
            '<div class="input-row">' +
                '<input type="time" id="schOn-' + safeId + '" class="card-input" value="' + (data.SchOn||'08:00') + '">' +
                '<input type="time" id="schOff-' + safeId + '" class="card-input" value="' + (data.SchOff||'18:00') + '">' +
                '<button class="inline-save-btn" onclick="window.saveSchedule(\'' + safeId + '\')">Save</button>' +
            '</div>' +
            '<div class="attr-row-toggle" style="margin-top:10px;">' +
                '<span style="font-size:0.85rem;color:var(--muted);">Enable</span>' +
                '<label class="toggle-switch"><input type="checkbox" onchange="window.sf(\'' + safeId + '\',\'isScheduleEnabled\',this.checked)"' + (data.isScheduleEnabled?' checked':'') + '><span class="toggle-track"><span class="toggle-thumb"></span></span></label>' +
            '</div>'
        ));
    }

    // Alarm
    if (attrs['Set Alarm']) {
        blocks.push(wrap('Set Alarm',
            '<span class="attr-pill-label">Alarm Setup</span>' +
            '<div class="input-row">' +
                '<input type="time" id="alarm-' + safeId + '" class="card-input" value="' + (data.AlarmTime||'07:00') + '">' +
                '<button class="inline-save-btn" onclick="window.sf(\'' + safeId + '\',\'AlarmTime\',document.getElementById(\'alarm-' + safeId + '\').value)">Save</button>' +
            '</div>' +
            '<div class="attr-row-toggle" style="margin-top:10px;">' +
                '<span style="font-size:0.85rem;color:var(--muted);">Enable</span>' +
                '<label class="toggle-switch"><input type="checkbox" onchange="window.sf(\'' + safeId + '\',\'isAlarmEnabled\',this.checked)"' + (data.isAlarmEnabled?' checked':'') + '><span class="toggle-track"><span class="toggle-thumb"></span></span></label>' +
            '</div>'
        ));
    }

    // Standard toggles
    if (attrs['NightMode'] || attrs['Night Mode'])   blocks.push(tog('Night Mode',     'NightMode',   data.NightMode,  'NightMode'));
    if (attrs['Set Internet Radio Mode'])            blocks.push(tog('Internet Radio', 'RadioMode',   data.RadioMode,  'Set Internet Radio Mode'));

    // Email Notification — input + Update button + enable toggle
    if (attrs['Set email Notification']) {
        blocks.push(wrap('Set email Notification',
            '<span class="attr-pill-label">Email Alert</span>' +
            '<div class="input-row" style="margin-bottom:10px;">' +
                '<input type="email" id="alertEmail-' + safeId + '" class="card-input" value="' + escapeHtml(data.AlertEmail||'') + '" placeholder="Alert email address">' +
                '<button class="inline-save-btn" onclick="window.saveAlertEmail(\'' + safeId + '\')">Update to Device</button>' +
            '</div>' +
            '<div class="attr-row-toggle">' +
                '<span style="font-size:0.85rem;color:var(--muted);">Enable Email Alert</span>' +
                '<label class="toggle-switch"><input type="checkbox" onchange="window.sf(\'' + safeId + '\',\'isEmailAlertON\',this.checked)"' + (data.isEmailAlertON?' checked':'') + '><span class="toggle-track"><span class="toggle-thumb"></span></span></label>' +
            '</div>'
        ));
    }

    // SMS Notification — input + Update button + enable toggle
    if (attrs['Set SMS Notification']) {
        blocks.push(wrap('Set SMS Notification',
            '<span class="attr-pill-label">SMS Alert</span>' +
            '<div class="input-row" style="margin-bottom:10px;">' +
                '<input type="tel" id="alertPhone-' + safeId + '" class="card-input" value="' + escapeHtml(data.AlertPhoneNumber||'') + '" placeholder="Alert phone number">' +
                '<button class="inline-save-btn" onclick="window.saveAlertPhone(\'' + safeId + '\')">Update to Device</button>' +
            '</div>' +
            '<div class="attr-row-toggle">' +
                '<span style="font-size:0.85rem;color:var(--muted);">Enable SMS Alert</span>' +
                '<label class="toggle-switch"><input type="checkbox" onchange="window.sf(\'' + safeId + '\',\'isSMSAlertON\',this.checked)"' + (data.isSMSAlertON?' checked':'') + '><span class="toggle-track"><span class="toggle-thumb"></span></span></label>' +
            '</div>'
        ));
    }

    // ── Layout ────────────────────────────────────────────────
    var layout = getColumnLayout(attrs);
    var html   = '';

    if (layout === 'colour-split' && colourHtml) {
        var half = Math.ceil(blocks.length / 2);
        var c1   = blocks.slice(0, half).join('');
        var c2   = blocks.slice(half).join('');
        html =
            '<div class="ctrl-layout-colour-split">' +
                '<div class="ctrl-col ctrl-col-colour">' + colourHtml + '</div>' +
                '<div class="ctrl-vdivider"></div>' +
                '<div class="ctrl-col-right">' +
                    '<div class="ctrl-col ctrl-col-sm">' + c1 + '</div>' +
                    (c2 ? '<div class="ctrl-vdivider"></div><div class="ctrl-col ctrl-col-sm">' + c2 + '</div>' : '') +
                '</div>' +
            '</div>';
    } else if (layout === 'two-col') {
        var half2 = Math.ceil(blocks.length / 2);
        var col1  = (colourHtml || '') + blocks.slice(0, half2).join('');
        var col2  = blocks.slice(half2).join('');
        html =
            '<div class="ctrl-layout-2col">' +
                '<div class="ctrl-col">' + col1 + '</div>' +
                '<div class="ctrl-vdivider"></div>' +
                '<div class="ctrl-col">' + col2 + '</div>' +
            '</div>';
    } else {
        html = '<div class="ctrl-layout-1col">' + colourHtml + blocks.join('') + '</div>';
    }

    container.innerHTML = html;
    setTimeout(function(){ initKnobs(safeId); }, 120);
}

// ── Knobs ─────────────────────────────────────────────────────
function initKnobs(safeId) {
    function updateRGB() {
        var r = $('.kr-' + safeId).val() || 0;
        var g = $('.kg-' + safeId).val() || 0;
        var b = $('.kb-' + safeId).val() || 0;
        $('#preview-' + safeId).css('background', 'rgb(' + r + ',' + g + ',' + b + ')');
        $('#preset-'  + safeId).val('RGB Mix');
    }
    // Common settings: 250° arc prevents wrap-around, displayInput centers value in knob
    var knobCommon = {
        angleOffset: -125,
        angleArc: 250,
        displayInput: true,
        inputColor: '#1a2a6c',
        font: 'Plus Jakarta Sans',
        fontWeight: '700'
    };
    
    $('.kr-' + safeId).knob(Object.assign({}, knobCommon, { fgColor:'#e74c3c', width:98, height:98, change:updateRGB }));
    $('.kg-' + safeId).knob(Object.assign({}, knobCommon, { fgColor:'#2ecc71', width:98, height:98, change:updateRGB }));
    $('.kb-' + safeId).knob(Object.assign({}, knobCommon, { fgColor:'#3498db', width:98, height:98, change:updateRGB }));
    // Brightness knob: visually distinct (bordered container + different knob styling), same horizontal level
    $('.kbright-' + safeId).knob(Object.assign({}, knobCommon, { fgColor:'#f1c40f', bgColor:'#fff8e1', thickness:0.22, lineCap:'round', width:98, height:98, release:function(v){ var scaled = Math.round(v * 255 / 100); window.sf(safeId,'BRIGHTNESS', scaled); if (v > 60) showToast('TOO MUCH BRIGHTNESS CAUSES, MORE POWER CONSUMPTION, MORE HEAT & ALSO HARMFULL FOR EYES','error'); } }));
    $('.kspeed-' + safeId).knob(Object.assign({}, knobCommon, { fgColor:'#1a2a6c', width:90, release:function(v){ window.sf(safeId,'Speed',v); } }));
    $('.kvol-'   + safeId).knob(Object.assign({}, knobCommon, { fgColor:'#27ae60', width:90, release:function(v){ window.sf(safeId,'Volume',v); } }));
    $('.kpower-' + safeId).knob(Object.assign({}, knobCommon, { fgColor:'#f24e1e', width:90, release:function(v){ window.sf(safeId,'PowerLevel',v); } }));
    $('.ksense-' + safeId).knob(Object.assign({}, knobCommon, { fgColor:'#b21f1f', width:90, release:function(v){ window.sf(safeId,'Sensitivity',v); } }));
}

// ── DB helpers (all use safeId — no device name in inline HTML) ──
function getCurrentUid() { return auth.currentUser && auth.currentUser.uid; }

function getDevicePath(safeId) {
    var name = getNameBySafeId(safeId);
    var uid  = getCurrentUid();
    return uid ? 'users/' + uid + '/devices/' + name : null;
}

// sf = saveField — short name avoids long inline strings
window.sf = function(safeId, field, value) {
    var path = getDevicePath(safeId);
    if (!path) return;
    var obj = {};
    obj[field] = value;
    update(ref(db, path), obj);
};

window.applyRGB = function(safeId) {
    var r = parseInt($('.kr-' + safeId).val()) || 0;
    var g = parseInt($('.kg-' + safeId).val()) || 0;
    var b = parseInt($('.kb-' + safeId).val()) || 0;
    var path = getDevicePath(safeId);
    if (!path) return;
    update(ref(db, path), { RED:r, GREEN:g, BLUE:b })
        .then(function(){ showToast('Color applied!','success'); })
        .catch(function(){ showToast('Failed.','error'); });
};

window.applyPreset = function(safeId, presetName) {
    var rgb = colourPresets[presetName];
    if (!rgb) return;
    var r=rgb[0], g=rgb[1], b=rgb[2];
    $('.kr-' + safeId).val(r).trigger('change');
    $('.kg-' + safeId).val(g).trigger('change');
    $('.kb-' + safeId).val(b).trigger('change');
    $('#preview-' + safeId).css('background','rgb('+r+','+g+','+b+')');
};

window.saveTemp = function(safeId) {
    var on  = document.getElementById('tOn-'  + safeId);
    var off = document.getElementById('tOff-' + safeId);
    var path = getDevicePath(safeId);
    if (!path || !on || !off) return;
    update(ref(db, path), { OnSetPoint:on.value, OffSetPoint:off.value })
        .then(function(){ showToast('Temperature saved!','success'); })
        .catch(function(){ showToast('Save failed.','error'); });
};

window.saveSchedule = function(safeId) {
    var on  = document.getElementById('schOn-'  + safeId);
    var off = document.getElementById('schOff-' + safeId);
    var path = getDevicePath(safeId);
    if (!path || !on || !off) return;
    update(ref(db, path), { SchOn:on.value, SchOff:off.value })
        .then(function(){ showToast('Schedule saved!','success'); })
        .catch(function(){ showToast('Save failed.','error'); });
};

window.saveAlertEmail = function(safeId) {
    var el   = document.getElementById('alertEmail-' + safeId);
    var path = getDevicePath(safeId);
    if (!path || !el) return;
    update(ref(db, path), { AlertEmail: el.value })
        .then(function(){ showToast('Alert email updated!','success'); })
        .catch(function(){ showToast('Save failed.','error'); });
};

window.saveAlertPhone = function(safeId) {
    var el   = document.getElementById('alertPhone-' + safeId);
    var path = getDevicePath(safeId);
    if (!path || !el) return;
    update(ref(db, path), { AlertPhoneNumber: el.value })
        .then(function(){ showToast('Alert phone updated!','success'); })
        .catch(function(){ showToast('Save failed.','error'); });
};

window.logout = function() { signOut(auth).then(function(){ window.location.href = "index.html"; }); };

// ── Toast ─────────────────────────────────────────────────────
function showToast(message, type) {
    var ex = document.getElementById('toast-msg');
    if (ex) ex.remove();
    var t = document.createElement('div');
    t.id = 'toast-msg';
    t.textContent = message;
    Object.assign(t.style, {
        position:'fixed', bottom:'28px', right:'28px', padding:'13px 22px',
        borderRadius:'12px', color:'white', fontFamily:'inherit', fontSize:'0.9rem',
        fontWeight:'600', zIndex:'9999', boxShadow:'0 8px 24px rgba(0,0,0,0.15)',
        background: type==='success' ? '#27ae60' : '#c0392b',
        transition:'opacity 0.3s', opacity:'1'
    });
    document.body.appendChild(t);
    setTimeout(function(){ t.style.opacity='0'; setTimeout(function(){ t.remove(); },300); }, 3200);
}

// ── Add Device ────────────────────────────────────────────────
var addTypeEl = document.getElementById('deviceTypeSelect');
if (addTypeEl) {
    addTypeEl.addEventListener('change', function() {
        var container = document.getElementById('attributeCheckboxes');
        container.innerHTML = '';
        (deviceMaster[this.value] || []).forEach(function(attr) {
            var lbl = document.createElement('label');
            lbl.innerHTML = '<input type="checkbox" name="attr" value="' + attr + '" checked> ' + attr;
            container.appendChild(lbl);
        });
    });
}

var addOtpBtn = document.getElementById('sendAddOtpBtn');
if (addOtpBtn) {
    addOtpBtn.addEventListener('click', function() {
        if (Object.keys(currentDevices).length >= MAX_DEVICES) {
            showToast('Max ' + MAX_DEVICES + ' devices reached.','error');
            window.showSection('live-control', null); return;
        }
        var nameEl = document.getElementById('newDeviceName');
        var name   = nameEl ? nameEl.value.trim() : '';
        if (!name) { showToast('Please enter a device name.','error'); return; }
        if (currentDevices[name]) { showToast('Device name already exists.','error'); return; }
        currentOtp = String(Math.floor(1000 + Math.random() * 9000));
        showToast('Your OTP: ' + currentOtp, 'success');
        var sec = document.getElementById('addOtpSection');
        if (sec) sec.classList.remove('hidden');
    });
}

var verifyAddBtn = document.getElementById('verifyAddBtn');
if (verifyAddBtn) {
    verifyAddBtn.addEventListener('click', async function() {
        var input = getOTPValue('add-otp-inputs');
        if (input !== currentOtp) { showToast('Invalid OTP.','error'); return; }
        var n    = document.getElementById('newDeviceName').value.trim();
        var type = document.getElementById('deviceTypeSelect').value;
        var attrs = {};
        document.querySelectorAll('input[name="attr"]:checked').forEach(function(cb){ attrs[cb.value]=true; });
        await set(ref(db, 'users/' + auth.currentUser.uid + '/devices/' + n), { type:type, attributes:attrs, isON:false });
        showToast('"' + n + '" added!','success');
        currentOtp = null;
        document.getElementById('newDeviceName').value = '';
        document.getElementById('deviceTypeSelect').value = '';
        document.getElementById('attributeCheckboxes').innerHTML = '';
        var sec = document.getElementById('addOtpSection'); if (sec) sec.classList.add('hidden');
        clearOTPFields('add-otp-inputs');
        // Activate Live Control nav link
        document.querySelectorAll('.nav-links a').forEach(function(a){ a.classList.remove('active'); });
        var liveLink = document.querySelector('.nav-links a[onclick*="live-control"]');
        if (liveLink) liveLink.classList.add('active');
        window.showSection('live-control', null);
    });
}

// ── Manage Device ─────────────────────────────────────────────
var manSel = document.getElementById('manageDeviceSelect');
if (manSel) {
    manSel.addEventListener('change', function() {
        var name    = this.value;
        var actions = document.getElementById('manageActions');
        var typeDiv = document.getElementById('manageDeviceType');
        var typeSel = document.getElementById('manageTypeSelect');
        var attrsEl = document.getElementById('manageAttributes');
        
        if (!name || name==='none') { 
            if (actions) actions.style.display='none';
            if (typeDiv) typeDiv.style.display='none';
            return; 
        }
        
        if (actions) actions.style.display='block';
        if (typeDiv) typeDiv.style.display='block';
        
        var dev = currentDevices[name];
        
        // Set device type dropdown
        if (typeSel) typeSel.value = dev.type;
        
        // Populate attributes for current type
        attrsEl.innerHTML = '';
        (deviceMaster[dev.type] || []).forEach(function(attr) {
            var checked = (dev.attributes && dev.attributes[attr]) ? 'checked' : '';
            var lbl = document.createElement('label');
            lbl.innerHTML = '<input type="checkbox" class="m-attr" value="' + attr + '" ' + checked + '> ' + attr;
            attrsEl.appendChild(lbl);
        });
    });
}

// Handle device type change in manage section
var manTypeSel = document.getElementById('manageTypeSelect');
if (manTypeSel) {
    manTypeSel.addEventListener('change', function() {
        var newType = this.value;
        var attrsEl = document.getElementById('manageAttributes');
        
        // Clear and repopulate attributes for new type (all checked by default)
        attrsEl.innerHTML = '';
        (deviceMaster[newType] || []).forEach(function(attr) {
            var lbl = document.createElement('label');
            lbl.innerHTML = '<input type="checkbox" class="m-attr" value="' + attr + '" checked> ' + attr;
            attrsEl.appendChild(lbl);
        });
    });
}

var updateBtn = document.getElementById('updateDeviceBtn');
if (updateBtn) {
    updateBtn.addEventListener('click', async function() {
        var name = document.getElementById('manageDeviceSelect').value;
        if (!name || name==='none') return;
        
        var newType = document.getElementById('manageTypeSelect').value;
        var attrs = {};
        document.querySelectorAll('.m-attr:checked').forEach(function(cb){ attrs[cb.value]=true; });
        
        // Save both type and attributes
        await update(ref(db, 'users/' + auth.currentUser.uid + '/devices/' + name), { 
            type: newType,
            attributes: attrs 
        });
        showToast('Device updated!','success');
    });
}

var delOtpBtn = document.getElementById('sendDeleteOtpBtn');
if (delOtpBtn) {
    delOtpBtn.addEventListener('click', function() {
        currentOtp = String(Math.floor(1000 + Math.random() * 9000));
        showToast('Deletion OTP: ' + currentOtp,'error');
        var sec = document.getElementById('deleteOtpSection'); if (sec) sec.classList.remove('hidden');
    });
}

var verifyDelBtn = document.getElementById('verifyDeleteBtn');
if (verifyDelBtn) {
    verifyDelBtn.addEventListener('click', async function() {
        var input = getOTPValue('delete-otp-inputs');
        if (input !== currentOtp) { showToast('OTP mismatch.','error'); return; }
        var name = document.getElementById('manageDeviceSelect').value;
        await remove(ref(db, 'users/' + auth.currentUser.uid + '/devices/' + name));
        showToast('"' + name + '" deleted.','success');
        currentOtp = null;
        var sec = document.getElementById('deleteOtpSection'); if (sec) sec.classList.add('hidden');
        clearOTPFields('delete-otp-inputs');
        var actions = document.getElementById('manageActions'); if (actions) actions.style.display='none';
    });
}

// OTP auto-advance + backspace
document.querySelectorAll('.otp-box').forEach(function(box, i, all) {
    box.addEventListener('input',   function(){ if (box.value && i < all.length-1) all[i+1].focus(); });
    box.addEventListener('keydown', function(e){ if (e.key==='Backspace' && !box.value && i>0) all[i-1].focus(); });
});

// ── Populate manage select ────────────────────────────────────
function populateManageSelect() {
    var sel = document.getElementById('manageDeviceSelect');
    if (!sel) return;
    var prev = sel.value;
    sel.innerHTML = '<option value="none" disabled selected>Choose Device</option>';
    Object.keys(currentDevices).sort().forEach(function(name) {
        var opt = document.createElement('option');
        opt.value = name; opt.textContent = name;
        sel.appendChild(opt);
    });
    if (prev && currentDevices[prev]) {
        sel.value = prev;
        sel.dispatchEvent(new Event('change'));
    }
}

// ── Utilities ─────────────────────────────────────────────────
function getOTPValue(id) {
    return Array.from(document.querySelectorAll('#' + id + ' .otp-box')).map(function(b){ return b.value; }).join('');
}
function clearOTPFields(id) {
    document.querySelectorAll('#' + id + ' .otp-box').forEach(function(b){ b.value=''; });
}
function sanitizeId(str) {
    return str.replace(/\s+/g,'-').replace(/[^a-zA-Z0-9\-_]/g,'');
}
function escapeHtml(str) {
    return String(str).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}
