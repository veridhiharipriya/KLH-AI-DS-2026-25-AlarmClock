const $ = (selector) => document.querySelector(selector);
const state = { alarms: [], history: [], lastFired: new Set(), initialized: false, timerEnd: 0, timerInterval: null, stopwatchStart: 0, stopwatchElapsed: 0, stopwatchInterval: null, laps: [], audioContext: null, ringInterval: null, ringOscillators: new Set(), activeRings: [], worldZones: ['Europe/London', 'America/New_York', 'Asia/Tokyo'] };
const cityNames = {
  'Europe/London': 'London', 'America/New_York': 'New York', 'Asia/Tokyo': 'Tokyo',
  'Europe/Paris': 'Paris', 'Asia/Kolkata': 'Mumbai', 'Australia/Sydney': 'Sydney',
  'America/Los_Angeles': 'Los Angeles', 'Asia/Dubai': 'Dubai', 'Pacific/Auckland': 'Auckland',
  'Africa/Cairo': 'Cairo', 'America/Sao_Paulo': 'São Paulo', 'Asia/Singapore': 'Singapore',
  'America/Chicago': 'Chicago', 'America/Denver': 'Denver', 'America/Toronto': 'Toronto',
  'America/Vancouver': 'Vancouver', 'America/Mexico_City': 'Mexico City',
  'America/Bogota': 'Bogota', 'America/Lima': 'Lima', 'America/Argentina/Buenos_Aires': 'Buenos Aires',
  'Europe/Lisbon': 'Lisbon', 'Europe/Madrid': 'Madrid', 'Europe/Berlin': 'Berlin',
  'Europe/Rome': 'Rome', 'Europe/Moscow': 'Moscow', 'Africa/Johannesburg': 'Johannesburg',
  'Africa/Nairobi': 'Nairobi', 'Asia/Jerusalem': 'Jerusalem', 'Asia/Riyadh': 'Riyadh',
  'Asia/Karachi': 'Karachi', 'Asia/Dhaka': 'Dhaka', 'Asia/Bangkok': 'Bangkok',
  'Asia/Shanghai': 'Shanghai', 'Asia/Seoul': 'Seoul', 'Asia/Manila': 'Manila',
  'Asia/Jakarta': 'Jakarta', 'Australia/Perth': 'Perth', 'Pacific/Honolulu': 'Honolulu'
};
const timezoneAliases = {
  'Asia/Calcutta': 'Asia/Kolkata',
  'Asia/Katmandu': 'Asia/Kathmandu',
  'Asia/Rangoon': 'Asia/Yangon',
  'Europe/Kiev': 'Europe/Kyiv',
  'America/Godthab': 'America/Nuuk',
  'Pacific/Truk': 'Pacific/Chuuk',
  'Pacific/Ponape': 'Pacific/Pohnpei'
};
const mapCities = [
  { zone: 'America/Vancouver', lat: 49.3, lon: -123.1 },
  { zone: 'America/Los_Angeles', lat: 34.1, lon: -118.2 },
  { zone: 'America/Denver', lat: 39.7, lon: -105 },
  { zone: 'America/Chicago', lat: 41.9, lon: -87.6 },
  { zone: 'America/New_York', lat: 40.7, lon: -74 },
  { zone: 'America/Toronto', lat: 43.7, lon: -79.4 },
  { zone: 'America/Mexico_City', lat: 19.4, lon: -99.1 },
  { zone: 'America/Bogota', lat: 4.7, lon: -74.1 },
  { zone: 'America/Lima', lat: -12, lon: -77 },
  { zone: 'America/Sao_Paulo', lat: -23.5, lon: -46.6 },
  { zone: 'America/Argentina/Buenos_Aires', lat: -34.6, lon: -58.4 },
  { zone: 'Europe/London', lat: 51.5, lon: -.1 },
  { zone: 'Europe/Lisbon', lat: 38.7, lon: -9.1 },
  { zone: 'Europe/Madrid', lat: 40.4, lon: -3.7 },
  { zone: 'Europe/Paris', lat: 48.9, lon: 2.4 },
  { zone: 'Europe/Berlin', lat: 52.5, lon: 13.4 },
  { zone: 'Europe/Rome', lat: 41.9, lon: 12.5 },
  { zone: 'Europe/Moscow', lat: 55.8, lon: 37.6 },
  { zone: 'Africa/Cairo', lat: 30, lon: 31.2 },
  { zone: 'Africa/Nairobi', lat: -1.3, lon: 36.8 },
  { zone: 'Africa/Johannesburg', lat: -26.2, lon: 28 },
  { zone: 'Asia/Jerusalem', lat: 31.8, lon: 35.2 },
  { zone: 'Asia/Riyadh', lat: 24.7, lon: 46.7 },
  { zone: 'Asia/Dubai', lat: 25.2, lon: 55.3 },
  { zone: 'Asia/Karachi', lat: 24.9, lon: 67 },
  { zone: 'Asia/Kolkata', lat: 19.1, lon: 72.9 },
  { zone: 'Asia/Dhaka', lat: 23.8, lon: 90.4 },
  { zone: 'Asia/Bangkok', lat: 13.8, lon: 100.5 },
  { zone: 'Asia/Jakarta', lat: -6.2, lon: 106.8 },
  { zone: 'Asia/Singapore', lat: 1.3, lon: 103.8 },
  { zone: 'Asia/Shanghai', lat: 31.2, lon: 121.5 },
  { zone: 'Asia/Manila', lat: 14.6, lon: 121 },
  { zone: 'Asia/Seoul', lat: 37.6, lon: 127 },
  { zone: 'Asia/Tokyo', lat: 35.7, lon: 139.7 },
  { zone: 'Australia/Perth', lat: -31.9, lon: 115.9 },
  { zone: 'Australia/Sydney', lat: -33.9, lon: 151.2 },
  { zone: 'Pacific/Auckland', lat: -36.9, lon: 174.8 }
];
let toastTimeout;

function showToast(message) {
  const toast = $('#toast');
  toast.textContent = message;
  toast.classList.add('show');
  clearTimeout(toastTimeout);
  toastTimeout = setTimeout(() => toast.classList.remove('show'), 3200);
}

function formatTime(date, zone) {
  return new Intl.DateTimeFormat(undefined, { timeZone: zone, hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false }).format(date);
}

function formatDate(date, zone) {
  return new Intl.DateTimeFormat(undefined, { timeZone: zone, weekday: 'short', month: 'short', day: 'numeric' }).format(date);
}

function renderClock() {
  const now = new Date();
  $('#live-clock').textContent = formatTime(now);
  $('#today-label').textContent = new Intl.DateTimeFormat(undefined, { weekday: 'long', month: 'long', day: 'numeric' }).format(now);
  $('#hero-date').textContent = Intl.DateTimeFormat().resolvedOptions().timeZone;
  $('#local-zone').textContent = `${Intl.DateTimeFormat().resolvedOptions().timeZone} · ${new Intl.DateTimeFormat(undefined, { dateStyle: 'full' }).format(now)}`;
  document.querySelectorAll('.world-card').forEach((card) => {
    const zone = card.dataset.zone;
    card.querySelector('.world-time').textContent = formatTime(now, zone);
    card.querySelector('.world-date').textContent = formatDate(now, zone);
    const localHour = Number(new Intl.DateTimeFormat('en', { timeZone: Intl.DateTimeFormat().resolvedOptions().timeZone, hour: 'numeric', hour12: false }).format(now));
    const cityHour = Number(new Intl.DateTimeFormat('en', { timeZone: zone, hour: 'numeric', hour12: false }).format(now));
    let difference = cityHour - localHour;
    if (difference > 12) difference -= 24;
    if (difference < -12) difference += 24;
    card.querySelector('.world-diff').textContent = difference === 0 ? 'SAME AS YOUR TIME' : `${difference > 0 ? '+' : ''}${difference} HRS FROM YOU`;
  });
}

function renderWorldClocks() {
  $('#world-clocks').innerHTML = state.worldZones.map((zone) => `
    <article class="world-card" data-zone="${zone}">
      <div class="world-city">${escapeHtml(cityLabel(zone).toUpperCase())}</div>
      <div class="world-time">--:--:--</div><div class="world-date">—</div><div class="world-diff">—</div>
      <button class="remove-world-city" type="button" aria-label="Remove ${escapeHtml(cityLabel(zone))}">×</button>
    </article>`).join('');
  document.querySelectorAll('.remove-world-city').forEach((button) => {
    button.addEventListener('click', () => {
      const zone = button.closest('.world-card').dataset.zone;
      state.worldZones = state.worldZones.filter((item) => item !== zone);
      renderWorldClocks();
      renderMapMarkers();
    });
  });
  renderMapMarkers();
  renderClock();
}

function cityLabel(zone) {
  return cityNames[zone] || zone.split('/').pop().replaceAll('_', ' ');
}

function canonicalTimezone(zone) {
  return timezoneAliases[zone] || zone;
}

function renderMapMarkers() {
  const group = $('#map-markers');
  group.replaceChildren(...mapCities.map((city) => {
    const x = ((city.lon + 180) / 360) * 1000;
    const y = ((90 - city.lat) / 180) * 460;
    const marker = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    marker.classList.add('map-marker');
    marker.setAttribute('transform', `translate(${x} ${y})`);
    marker.setAttribute('role', 'button');
    marker.setAttribute('tabindex', '0');
    marker.setAttribute('aria-label', `${cityLabel(city.zone)}${state.worldZones.includes(city.zone) ? ', selected' : ', add to world clock'}`);
    marker.setAttribute('aria-pressed', String(state.worldZones.includes(city.zone)));
    marker.dataset.zone = city.zone;
    marker.innerHTML = '<circle class="marker-halo" r="10"></circle><circle class="marker-dot" r="4"></circle>';
    const title = document.createElementNS('http://www.w3.org/2000/svg', 'title');
    title.textContent = cityLabel(city.zone);
    marker.append(title);
    marker.addEventListener('click', () => addWorldCity(city.zone));
    marker.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        addWorldCity(city.zone);
      }
    });
    return marker;
  }));
}

function populateTimezoneChoices() {
  const zones = typeof Intl.supportedValuesOf === 'function'
    ? Intl.supportedValuesOf('timeZone')
    : Object.keys(cityNames);
  const options = $('#timezone-options');
  zones.forEach((zone) => {
    const option = document.createElement('option');
    option.value = `${cityLabel(zone)} — ${zone}`;
    options.append(option);
  });
}

function addWorldCity(zone) {
  if (state.worldZones.includes(zone)) {
    showToast(`${cityLabel(zone)} is already on your world clock.`);
    return;
  }
  state.worldZones.push(zone);
  $('#timezone-search').value = '';
  renderWorldClocks();
}

function renderAlarms() {
  const container = $('#alarm-list');
  if (!state.alarms.length) {
    container.innerHTML = '<div class="empty-card">No alarms yet. Set one to make your next moment count.</div>';
    return;
  }
  container.replaceChildren(...state.alarms.slice().sort((a, b) => a.when - b.when).map((alarm) => {
    const card = document.createElement('article');
    card.className = 'alarm-card';
    const date = new Date(alarm.when * 1000);
    const time = document.createElement('div');
    const status = alarm.enabled ? 'ACTIVE' : (alarm.when * 1000 <= Date.now() ? 'FIRED' : 'PAUSED');
    const repeatLabel = alarm.repeatDays ? ` · EVERY ${formatWeekdays(alarm.repeatDays)}` : '';
    time.innerHTML = `<div class="alarm-time">${escapeHtml(new Intl.DateTimeFormat(undefined, { hour: '2-digit', minute: '2-digit' }).format(date))}</div><div class="alarm-name">${escapeHtml(alarm.label)}</div><div class="alarm-meta">${escapeHtml(new Intl.DateTimeFormat(undefined, { weekday: 'short', month: 'short', day: 'numeric' }).format(date))} · ${status}${repeatLabel}</div>`;
    const actions = document.createElement('div');
    actions.className = 'alarm-actions';
    const fired = !alarm.enabled && alarm.when * 1000 <= Date.now();
    actions.innerHTML = `${fired ? `<button class="snooze-alarm secondary-button" type="button">Snooze 5m</button>` : ''}<input class="toggle" type="checkbox" aria-label="Toggle ${escapeHtml(alarm.label)}" ${alarm.enabled ? 'checked' : ''} ${alarm.when * 1000 <= Date.now() ? 'disabled' : ''}><button class="delete-alarm" type="button" aria-label="Delete ${escapeHtml(alarm.label)}">×</button>`;
    actions.querySelector('.toggle').addEventListener('change', () => toggleAlarm(alarm));
    actions.querySelector('.snooze-alarm')?.addEventListener('click', () => snoozeAlarm(alarm.id));
    actions.querySelector('.delete-alarm').addEventListener('click', () => deleteAlarm(alarm.id));
    card.append(time, actions);
    return card;
  }));
}

function formatWeekdays(mask) {
  return ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']
    .filter((_, day) => mask & (1 << day))
    .join(', ');
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (character) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
}

function renderHistory() {
  const container = $('#history-list');
  if (!state.history.length) {
    container.innerHTML = '<p class="muted">No alarms have fired yet.</p>';
    return;
  }
  container.replaceChildren(...state.history.slice().reverse().map((item) => {
    const row = document.createElement('div');
    row.className = 'history-row';
    const label = document.createElement('span');
    label.textContent = `◷  ${item.label}`;
    const time = document.createElement('time');
    time.textContent = new Intl.DateTimeFormat(undefined, { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' }).format(new Date(item.when * 1000));
    const remove = document.createElement('button');
    remove.className = 'delete-history';
    remove.type = 'button';
    remove.textContent = '×';
    remove.setAttribute('aria-label', `Delete ${item.label} from alarm history`);
    remove.addEventListener('click', () => deleteHistoryEntry(item));
    row.append(label, time, remove);
    return row;
  }));
}

async function deleteHistoryEntry(item) {
  try {
    const response = await fetch(`/api/history/${item.id}/${item.when}`, { method: 'DELETE' });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Could not delete history entry');
    showToast('Alarm removed from history.');
    await refreshState();
  } catch (error) {
    showToast(error.message);
  }
}

async function refreshState() {
  try {
    const response = await fetch('/api/state', { cache: 'no-store' });
    if (!response.ok) throw new Error(`Service returned ${response.status}`);
    const data = await response.json();
    state.alarms = data.alarms;
    state.history = data.history;
    for (const item of state.history) {
      const eventKey = `${item.id}:${item.when}`;
      if (!state.lastFired.has(eventKey)) {
        state.lastFired.add(eventKey);
        if (state.initialized) ringAlarm(item);
      }
    }
    state.initialized = true;
    renderAlarms();
    renderHistory();
  } catch (error) {
    console.error('Could not connect to the alarm service:', error);
  }
}

async function addAlarm(event) {
  event.preventDefault();
  const date = new Date($('#alarm-datetime').value);
  const error = $('#form-error');
  const repeatDays = [...document.querySelectorAll('.weekday-options input:checked')]
    .reduce((mask, input) => mask | (1 << Number(input.value)), 0);
  if (!$('#alarm-datetime').value || Number.isNaN(date.getTime())) {
    error.textContent = 'Choose a valid date and time.';
    return;
  }
  if (repeatDays) {
    while (!(repeatDays & (1 << date.getDay())) || date.getTime() <= Date.now()) {
      date.setDate(date.getDate() + 1);
    }
    const localDate = new Date(date.getTime() - date.getTimezoneOffset() * 60000)
      .toISOString().slice(0, 16);
    $('#alarm-datetime').value = localDate;
  } else if (date.getTime() <= Date.now()) {
    error.textContent = 'Choose a future date and time.';
    return;
  }
  try {
    const response = await fetch('/api/alarms', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        label: $('#alarm-label').value.trim() || 'Alarm',
        when: Math.floor(date.getTime() / 1000),
        days: repeatDays,
        timezone: canonicalTimezone(Intl.DateTimeFormat().resolvedOptions().timeZone)
      })
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Could not save alarm');
    $('#alarm-dialog').close();
    $('#alarm-form').reset();
    error.textContent = '';
    showToast('Alarm saved. The Ubuntu scheduler will watch for it.');
    await refreshState();
  } catch (requestError) {
    error.textContent = requestError.message;
  }
}

async function deleteAlarm(id) {
  try {
    const response = await fetch(`/api/alarms/${id}`, { method: 'DELETE' });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Could not delete alarm');
    showToast('Alarm removed.');
    await refreshState();
  } catch (error) {
    showToast(error.message);
  }
}

function toggleAlarm(alarm) {
  fetch(`/api/alarms/${alarm.id}/toggle`, { method: 'POST' })
    .then(async (response) => {
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || 'Could not update alarm');
      await refreshState();
    })
    .catch(async (error) => {
      showToast(error.message);
      await refreshState();
    });
}

async function snoozeAlarm(id) {
  try {
    const response = await fetch(`/api/alarms/${id}/snooze`, { method: 'POST' });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Could not snooze alarm');
    dismissRingingAlarm(id);
    showToast('Alarm snoozed for 5 minutes.');
    await refreshState();
  } catch (error) {
    showToast(error.message);
  }
}

function playAlarmPulse(context) {
  const oscillator = context.createOscillator();
  const gain = context.createGain();
  oscillator.frequency.value = 880;
  oscillator.type = 'sine';
  gain.gain.setValueAtTime(0.001, context.currentTime);
  gain.gain.linearRampToValueAtTime(0.18, context.currentTime + 0.04);
  gain.gain.setValueAtTime(0.18, context.currentTime + 0.35);
  gain.gain.linearRampToValueAtTime(0.001, context.currentTime + 0.5);
  oscillator.connect(gain);
  gain.connect(context.destination);
  state.ringOscillators.add(oscillator);
  oscillator.onended = () => state.ringOscillators.delete(oscillator);
  oscillator.start();
  oscillator.stop(context.currentTime + 0.52);
}

async function unlockAudio() {
  try {
    if (!state.audioContext) state.audioContext = new AudioContext();
    if (state.audioContext.state === 'suspended') await state.audioContext.resume();
  } catch (error) {
    console.warn('Could not enable alarm audio:', error);
  }
}

document.addEventListener('pointerdown', unlockAudio, { once: true });
document.addEventListener('keydown', unlockAudio, { once: true });

function ringAlarm(label) {
  const isClockAlarm = Number.isInteger(label.id);
  state.activeRings.push({ label: label.label || label, alarmId: isClockAlarm ? label.id : null });
  renderRingAlert();
  try {
    const context = state.audioContext || new AudioContext();
    state.audioContext = context;
    const startRepeatingSound = () => {
      if (!state.activeRings.length || state.ringInterval) return;
      playAlarmPulse(context);
      state.ringInterval = setInterval(() => {
        if (context.state === 'running') playAlarmPulse(context);
      }, 750);
    };
    if (context.state === 'suspended') {
      context.resume().then(startRepeatingSound).catch((error) => {
        console.warn('Could not start alarm audio:', error);
        showToast('Click the page once to allow alarm sound.');
      });
    } else {
      startRepeatingSound();
    }
  } catch (error) {
    console.warn('Audio notification unavailable:', error);
    showToast('Alarm sound is unavailable in this browser.');
  }
  if ('Notification' in window && Notification.permission === 'granted') {
    new Notification('Chronos alarm', { body: isClockAlarm ? label.label : label });
  }
}

function renderRingAlert() {
  const alert = $('#ring-alert');
  if (!state.activeRings.length) {
    alert.hidden = true;
    return;
  }
  alert.hidden = false;
  $('#ring-title').textContent = state.activeRings.length === 1
    ? `${state.activeRings[0].label} is ringing`
    : `${state.activeRings.length} alarms are ringing`;
  $('#ring-message').textContent = state.activeRings.map((item) => item.label).join(', ');
  $('#ring-snooze').hidden = !state.activeRings.some((item) => item.alarmId !== null);
}

function stopRinging() {
  if (state.ringInterval) {
    clearInterval(state.ringInterval);
    state.ringInterval = null;
  }
  for (const oscillator of state.ringOscillators) {
    try {
      oscillator.stop();
    } catch (error) {
      if (!(error instanceof DOMException && error.name === 'InvalidStateError')) {
        console.warn('Could not stop alarm audio:', error);
      }
    }
  }
  state.ringOscillators.clear();
  state.activeRings = [];
  renderRingAlert();
}

function dismissRingingAlarm(id) {
  state.activeRings = state.activeRings.filter((item) => item.alarmId !== id);
  if (!state.activeRings.length) {
    stopRinging();
  } else {
    renderRingAlert();
  }
}

async function snoozeRingingAlarms() {
  const alarmIds = state.activeRings
    .map((item) => item.alarmId)
    .filter((id) => id !== null);
  if (!alarmIds.length) return;
  try {
    await Promise.all(alarmIds.map(async (id) => {
      const response = await fetch(`/api/alarms/${id}/snooze`, { method: 'POST' });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || 'Could not snooze alarm');
    }));
    stopRinging();
    showToast('Alarm snoozed for 5 minutes.');
    await refreshState();
  } catch (error) {
    showToast(error.message);
  }
}

function timerValue() {
  const minutes = Math.max(0, Math.min(999, Number($('#timer-minutes').value) || 0));
  const seconds = Math.max(0, Math.min(59, Number($('#timer-seconds').value) || 0));
  return minutes * 60 + seconds;
}

function renderTimer() {
  if (!state.timerInterval && $('#timer-state').textContent !== 'PAUSED' && $('#timer-state').textContent !== 'DONE') {
    const seconds = timerValue();
    $('#timer-display').textContent = `${String(Math.floor(seconds / 3600)).padStart(2, '0')}:${String(Math.floor((seconds % 3600) / 60)).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`;
    return;
  }
  const remaining = Math.max(0, Math.ceil((state.timerEnd - Date.now()) / 1000));
  $('#timer-display').textContent = `${String(Math.floor(remaining / 3600)).padStart(2, '0')}:${String(Math.floor((remaining % 3600) / 60)).padStart(2, '0')}:${String(remaining % 60).padStart(2, '0')}`;
  if (remaining === 0 && state.timerInterval) {
    clearInterval(state.timerInterval);
    state.timerInterval = null;
    $('#timer-state').textContent = 'DONE';
    $('#timer-start').textContent = 'Start timer';
    ringAlarm({ label: 'Timer complete' });
  }
}

function startTimer() {
  if (state.timerInterval) {
    clearInterval(state.timerInterval);
    state.timerInterval = null;
    $('#timer-state').textContent = 'PAUSED';
    $('#timer-start').textContent = 'Resume';
    return;
  }
  if ($('#timer-state').textContent === 'PAUSED') {
    const [hours, minutes, seconds] = $('#timer-display').textContent.split(':').map(Number);
    state.timerEnd = Date.now() + ((hours * 3600 + minutes * 60 + seconds) * 1000);
  } else {
    const duration = timerValue();
    if (!duration) return showToast('Set a timer duration first.');
    state.timerEnd = Date.now() + duration * 1000;
  }
  $('#timer-state').textContent = 'RUNNING';
  $('#timer-start').textContent = 'Pause';
  state.timerInterval = setInterval(renderTimer, 200);
  renderTimer();
}

function renderStopwatch() {
  const elapsed = state.stopwatchElapsed + (state.stopwatchStart ? performance.now() - state.stopwatchStart : 0);
  const totalCentiseconds = Math.floor(elapsed / 10);
  const minutes = Math.floor(totalCentiseconds / 6000);
  const seconds = Math.floor((totalCentiseconds % 6000) / 100);
  const centiseconds = totalCentiseconds % 100;
  $('#stopwatch-display').textContent = `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}.${String(centiseconds).padStart(2, '0')}`;
}

function toggleStopwatch() {
  if (state.stopwatchInterval) {
    state.stopwatchElapsed += performance.now() - state.stopwatchStart;
    state.stopwatchStart = 0;
    clearInterval(state.stopwatchInterval);
    state.stopwatchInterval = null;
    $('#stopwatch-state').textContent = 'PAUSED';
    $('#stopwatch-start').textContent = 'Resume';
  } else {
    state.stopwatchStart = performance.now();
    state.stopwatchInterval = setInterval(renderStopwatch, 20);
    $('#stopwatch-state').textContent = 'RUNNING';
    $('#stopwatch-start').textContent = 'Pause';
    $('#stopwatch-lap').disabled = false;
  }
  renderStopwatch();
}

function addLap() {
  const elapsed = state.stopwatchElapsed + (state.stopwatchStart ? performance.now() - state.stopwatchStart : 0);
  state.laps.push(elapsed);
  $('#lap-list').innerHTML = state.laps.slice().reverse().map((lap, index) => {
    const cs = Math.floor(lap / 10);
    return `<div class="lap-row"><span>LAP ${state.laps.length - index}</span><strong>${String(Math.floor(cs / 6000)).padStart(2, '0')}:${String(Math.floor((cs % 6000) / 100)).padStart(2, '0')}.${String(cs % 100).padStart(2, '0')}</strong></div>`;
  }).join('');
}

$('#open-alarm').addEventListener('click', () => {
  const now = new Date(Date.now() + 5 * 60000);
  now.setSeconds(0, 0);
  const local = new Date(now.getTime() - now.getTimezoneOffset() * 60000).toISOString().slice(0, 16);
  $('#alarm-datetime').value = local;
  document.querySelectorAll('.weekday-options input').forEach((input) => {
    input.checked = false;
  });
  $('#form-error').textContent = '';
  $('#alarm-dialog').showModal();
});
$('#close-dialog').addEventListener('click', () => $('#alarm-dialog').close());
$('#alarm-form').addEventListener('submit', addAlarm);
$('#timer-start').addEventListener('click', startTimer);
$('#ring-stop').addEventListener('click', () => {
  stopRinging();
  showToast('Alarm sound stopped.');
});
$('#ring-snooze').addEventListener('click', snoozeRingingAlarms);
$('#timer-reset').addEventListener('click', () => {
  clearInterval(state.timerInterval);
  state.timerInterval = null;
  if (state.activeRings.some((item) => item.alarmId === null)) {
    state.activeRings = state.activeRings.filter((item) => item.alarmId !== null);
    if (!state.activeRings.length) stopRinging();
    else renderRingAlert();
  }
  $('#timer-state').textContent = 'READY';
  $('#timer-start').textContent = 'Start timer';
  $('#timer-minutes').value = '5';
  $('#timer-seconds').value = '0';
  renderTimer();
});
$('#timer-minutes').addEventListener('input', () => { if (!state.timerInterval && $('#timer-state').textContent !== 'PAUSED') renderTimer(); });
$('#timer-seconds').addEventListener('input', () => { if (!state.timerInterval && $('#timer-state').textContent !== 'PAUSED') renderTimer(); });
$('#stopwatch-start').addEventListener('click', toggleStopwatch);
$('#stopwatch-lap').addEventListener('click', addLap);
$('#stopwatch-reset').addEventListener('click', () => {
  clearInterval(state.stopwatchInterval);
  state.stopwatchInterval = null;
  state.stopwatchElapsed = 0;
  state.stopwatchStart = 0;
  state.laps = [];
  $('#stopwatch-state').textContent = 'READY';
  $('#stopwatch-start').textContent = 'Start';
  $('#stopwatch-lap').disabled = true;
  $('#lap-list').innerHTML = '<span class="muted">Laps will appear here</span>';
  renderStopwatch();
});
function addSearchedCity() {
  const value = $('#timezone-search').value.trim();
  const separator = value.lastIndexOf(' — ');
  const zone = separator < 0 ? value : value.slice(separator + 3);
  try {
    new Intl.DateTimeFormat(undefined, { timeZone: zone });
    addWorldCity(zone);
  } catch {
    showToast('Choose a city from the suggestions or select one on the map.');
  }
}

$('#add-timezone').addEventListener('click', addSearchedCity);
$('#timezone-search').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    event.preventDefault();
    addSearchedCity();
  }
});
$('#notify-button').addEventListener('click', async () => {
  if (!('Notification' in window)) return showToast('This browser does not support notifications.');
  const permission = await Notification.requestPermission();
  showToast(permission === 'granted' ? 'Desktop alarm notifications enabled.' : 'Notification permission was not granted.');
});

renderWorldClocks();
populateTimezoneChoices();
renderTimer();
renderStopwatch();
refreshState();
setInterval(renderClock, 1000);
setInterval(refreshState, 2000);
