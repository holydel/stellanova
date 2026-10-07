// Stella Nova's admin page: talks JSON to the game server's admin listener
// (docs/adr/0016-sign-in-and-admin.md, "The admin connection"). Caddy lets
// only WOS Observer's superusers reach the page and its socket, so the page
// has no login of its own. Names are player text: they always go in as text
// nodes, never as HTML.
'use strict';

(() => {
	const RETRY_SECONDS = 3;
	const STATUS_EVERY = 5000; // ms
	const ACCOUNT_EVERY = 10000;
	const TIMES_EVERY = 15000; // "3 h ago" texts
	const ANSWER_TIMEOUT = 15000;
	const SEARCH_DELAY = 300;
	const PAGE_SIZE = 50;

	const RESOURCES = [['metal', 'Metal'], ['he3', 'He-3'], ['chips', 'Chips']];
	const PROVIDERS = { steam: 'Steam', google: 'Google', apple: 'Apple', discord: 'Discord' };
	const PLACES = { hub: 'Hub', battle: 'Battle', skirmish: 'Skirmish' };
	// Errors the server answers with a word.
	const ERRORS = { online: 'Refused: the player is connected.' };

	const $ = (id) => document.getElementById(id);
	const decoder = new TextDecoder();
	const whole = new Intl.NumberFormat('en-US', { maximumFractionDigits: 0 });

	const state = {
		socket: null,
		connected: false,
		tag: 0,
		pending: new Map(), // tag -> { resolve, reject, timer }
		retryTimer: 0,
		retryAt: 0,
		timers: [], // run while connected
		skew: 0, // the server's clock minus ours, in s
		statusBusy: false,
		list: { query: '', filter: 'all', offset: 0, total: 0, items: [], seq: 0 },
		searchTimer: 0,
		open: '', // the open account's id
		answer: null, // its last `account` answer
		loading: '', // the account being fetched
		results: {}, // action -> { kind, text }
		busy: {}, // action -> true while its request runs
		rawOpen: false,
		flashTimer: 0,
	};

	// --- DOM helpers. Text always goes in as text nodes, never as HTML. ---

	function h(tag, props, ...kids) {
		const node = document.createElement(tag);
		if (props) {
			for (const [key, value] of Object.entries(props)) {
				if (value == null || value === false)
					continue;
				if (key === 'class')
					node.className = value;
				else if (typeof value === 'function')
					node.addEventListener(key.replace(/^on/, ''), value);
				else
					node.setAttribute(key, value === true ? '' : String(value));
			}
		}
		for (const kid of kids.flat(Infinity)) {
			if (kid != null && kid !== false)
				node.append(kid);
		}
		return node;
	}

	const isNum = (value) => typeof value === 'number' && Number.isFinite(value);
	const isTime = (value) => isNum(value) && value > 0;
	const asObject = (value) => (value && typeof value === 'object' && !Array.isArray(value) ? value : {});
	const asArray = (value) => (Array.isArray(value) ? value : []);
	const dim = (text) => h('span', { class: 'dim' }, text);
	// Server words go into class names: keep them to safe characters.
	const token = (text) => String(text).toLowerCase().replace(/[^a-z0-9-]/g, '');

	function count(value) {
		return isNum(value) ? whole.format(Math.floor(value)) : '-';
	}

	function describe(error) {
		const what = error && error.message ? error.message : String(error);
		return ERRORS[what] || 'Failed: ' + what;
	}

	// --- Times: the server's are seconds since 1970. ---

	const serverNow = () => Date.now() / 1000 + state.skew;
	const pad = (n) => String(n).padStart(2, '0');

	function stamp(seconds) {
		const d = new Date(seconds * 1000);
		return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' +
			pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
	}

	function clock() {
		const d = new Date();
		return pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
	}

	function duration(seconds) {
		const s = Math.max(0, Math.floor(seconds));
		if (s < 60)
			return s + ' s';
		if (s < 3600)
			return Math.floor(s / 60) + ' min';
		if (s < 86400)
			return Math.floor(s / 3600) + ' h ' + Math.floor((s % 3600) / 60) + ' min';
		return Math.floor(s / 86400) + ' d ' + Math.floor((s % 86400) / 3600) + ' h';
	}

	function ago(seconds) {
		const delta = serverNow() - seconds;
		const size = Math.abs(delta);
		if (size < 45)
			return 'just now';
		let text;
		if (size < 3600)
			text = Math.round(size / 60) + ' min';
		else if (size < 86400)
			text = Math.round(size / 3600) + ' h';
		else if (size < 86400 * 45)
			text = Math.round(size / 86400) + ' d';
		else if (size < 86400 * 548)
			text = Math.round(size / (86400 * 30.44)) + ' mo';
		else
			text = Math.round(size / (86400 * 365.25)) + ' y';
		return delta >= 0 ? text + ' ago' : 'in ' + text;
	}

	// "3 h ago", with the date in its title; the ticker keeps it fresh.
	function when(seconds) {
		if (!isTime(seconds))
			return dim('-');
		return h('time', { 'data-at': seconds, datetime: new Date(seconds * 1000).toISOString(), title: stamp(seconds) }, ago(seconds));
	}

	// The date, then "3 h ago".
	function whenFull(seconds) {
		if (!isTime(seconds))
			return dim('-');
		return [stamp(seconds), ' ', h('span', { class: 'dim' }, '(', when(seconds), ')')];
	}

	function tickTimes() {
		for (const node of document.querySelectorAll('time[data-at]'))
			node.textContent = ago(Number(node.dataset.at));
	}

	// --- Pieces ---

	function nameText(name) {
		return name ? String(name) : dim('(no name)');
	}

	function shortId(id) {
		const text = String(id || '');
		return h('span', { class: 'mono', title: text }, text.length > 8 ? text.slice(0, 8) : text);
	}

	const accountHref = (id) => '#a=' + encodeURIComponent(id);

	function badge(text, cls) {
		return h('span', { class: 'badge chip-cut ' + cls }, text);
	}

	function providerBadge(provider) {
		if (!provider)
			return badge('guest', 'guest');
		return badge(PROVIDERS[provider] || String(provider), 'p-' + token(provider));
	}

	// `providers` may list words or identities; none is a guest.
	function providerList(list) {
		const providers = asArray(list).map((entry) => (typeof entry === 'string' ? entry : asObject(entry).provider)).filter(Boolean);
		return providers.length ? providers.map(providerBadge) : [providerBadge('')];
	}

	function providerBadges(list) {
		return h('span', { class: 'badges' }, providerList(list));
	}

	function placeBadge(where) {
		return where ? badge(PLACES[where] || String(where), 'w-' + token(where)) : dim('-');
	}

	// The flags of an account summary or answer (the answer keeps banned and hidden in its data).
	function flagsOf(item, data) {
		const extra = asObject(data);
		return {
			banned: !!(item.banned ?? extra.banned),
			hidden: !!(item.hidden ?? extra.hidden),
			kept: !!(item.kept ?? extra.kept),
			online: !!item.online,
			unsaved: item.saved === false,
		};
	}

	function flagBadges(flags) {
		return h('span', { class: 'badges' }, flagList(flags));
	}

	function flagList(flags) {
		const badges = [];
		if (flags.online)
			badges.push(badge('online', 'f-online'));
		if (flags.unsaved)
			badges.push(badge('not saved', 'f-unsaved'));
		if (flags.kept)
			badges.push(badge('kept guest', 'f-hidden'));
		if (flags.banned)
			badges.push(badge('banned', 'f-banned'));
		if (flags.hidden)
			badges.push(badge('hidden', 'f-hidden'));
		return badges;
	}

	function conditionText(value) {
		if (!isNum(value))
			return dim('-');
		const percent = Math.round(value * 100);
		const cls = value < 0.5 ? 'cond-low' : value < 0.9 ? 'cond-mid' : null;
		return h('span', { class: cls, title: 'Condition' }, percent + '%');
	}

	// Stacks are {id: count} in the account's JSON; [{id, count}] is read too.
	function stacksOf(value) {
		if (Array.isArray(value))
			return value.map((entry) => [asObject(entry).id, asObject(entry).count]);
		return Object.entries(asObject(value));
	}

	function stackChips(value, none) {
		const stacks = stacksOf(value);
		if (!stacks.length)
			return dim(none);
		return h('div', { class: 'chips' }, stacks.map(([id, n]) => h('span', { class: 'chip chip-cut' }, String(id), h('b', null, count(n)))));
	}

	function tile(value, label, cls) {
		return h('div', { class: 'tile ' + (cls || '') }, h('b', null, value), h('span', null, label));
	}

	function section(title, note, ...kids) {
		return h('section', { class: 'sec' }, h('h3', null, title, note != null ? h('small', null, note) : null), kids);
	}

	function kvRow(label, ...value) {
		return [h('dt', null, label), h('dd', null, value)];
	}

	function flash(text, bad) {
		const node = $('flash');
		node.textContent = text;
		node.className = bad ? 'flash bad' : 'flash';
		node.hidden = false;
		clearTimeout(state.flashTimer);
		state.flashTimer = setTimeout(() => {
			node.hidden = true;
		}, 5000);
	}

	// --- The connection ---

	// ?ws= points the page at a server on this machine (local testing), never
	// elsewhere: a crafted link must not send an admin's actions to a stranger.
	function socketUrl() {
		const override = new URLSearchParams(location.search).get('ws');
		if (override && /^ws:\/\/(127\.0\.0\.1|localhost)(:\d+)?(\/|$)/.test(override))
			return override;
		return (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/stellanova/admin/ws';
	}

	function setConnection(kind, text) {
		const node = $('conn');
		node.dataset.state = kind;
		$('conn-text').textContent = text;
		$('reconnect').hidden = kind !== 'closed';
	}

	function connect() {
		clearTimeout(state.retryTimer);
		const url = socketUrl();
		$('conn').title = url;
		setConnection('connecting', 'Connecting...');
		let socket;
		try {
			socket = new WebSocket(url);
		}
		catch (error) {
			// A malformed ?ws= address.
			console.error(error);
			onClose(false);
			return;
		}
		socket.binaryType = 'arraybuffer';
		let opened = false;
		socket.onopen = () => {
			opened = true;
			if (state.socket === socket)
				onOpen();
		};
		socket.onmessage = (event) => {
			if (state.socket === socket)
				receive(event.data);
		};
		socket.onclose = () => {
			if (state.socket === socket)
				onClose(opened);
		};
		state.socket = socket;
	}

	function onOpen() {
		state.connected = true;
		setConnection('connected', 'Connected');
		$('hint').hidden = true;
		refreshStatus();
		loadAccounts();
		if (state.open)
			loadAccount();
		state.timers = [setInterval(refreshStatus, STATUS_EVERY), setInterval(refreshAccount, ACCOUNT_EVERY)];
	}

	function onClose(wasOpen) {
		state.socket = null;
		state.connected = false;
		state.statusBusy = false;
		state.loading = '';
		for (const timer of state.timers)
			clearInterval(timer);
		state.timers = [];
		for (const waiting of state.pending.values()) {
			clearTimeout(waiting.timer);
			waiting.reject(new Error('the connection closed'));
		}
		state.pending.clear();
		// Caddy refuses the upgrade, before it opens, when the admin is not signed in.
		$('hint').hidden = wasOpen;
		state.retryAt = Date.now() + RETRY_SECONDS * 1000;
		countdown();
	}

	function countdown() {
		const left = Math.ceil((state.retryAt - Date.now()) / 1000);
		if (left <= 0) {
			connect();
			return;
		}
		setConnection('closed', 'Closed, reconnecting in ' + left + ' s');
		state.retryTimer = setTimeout(countdown, 250);
	}

	function request(message) {
		return new Promise((resolve, reject) => {
			const socket = state.socket;
			if (!state.connected || !socket) {
				reject(new Error('not connected'));
				return;
			}
			const tag = ++state.tag;
			const timer = setTimeout(() => {
				state.pending.delete(tag);
				reject(new Error('no answer'));
			}, ANSWER_TIMEOUT);
			state.pending.set(tag, { resolve, reject, timer });
			socket.send(JSON.stringify(Object.assign({}, message, { tag })));
		});
	}

	// The server answers in binary frames of UTF-8 JSON; text frames are read too.
	function receive(data) {
		let message;
		try {
			message = JSON.parse(typeof data === 'string' ? data : decoder.decode(data));
		}
		catch (error) {
			console.warn('Not JSON from the server', error);
			return;
		}
		if (!message || typeof message !== 'object')
			return;
		// Answers show wherever they came from: any action returns the account.
		if (message.op === 'status')
			renderStatus(message);
		else if (message.op === 'account' && message.account === state.open)
			renderAccount(message);
		const waiting = state.pending.get(message.tag);
		if (waiting) {
			state.pending.delete(message.tag);
			clearTimeout(waiting.timer);
			if (message.op === 'error')
				waiting.reject(new Error(message.error || 'error'));
			else
				waiting.resolve(message);
		}
		else if (message.op === 'error') {
			flash('Server: ' + (message.error || 'error'), true);
		}
	}

	// --- Status: the header, the counts, who is online, the leaderboard ---

	async function refreshStatus() {
		if (state.statusBusy || !state.connected)
			return;
		state.statusBusy = true;
		try {
			await request({ op: 'status' }); // receive() shows it
			$('status-error').textContent = '';
		}
		catch (error) {
			if (state.connected)
				$('status-error').textContent = 'Status: ' + describe(error);
		}
		finally {
			state.statusBusy = false;
		}
	}

	function setCount(id, value) {
		$(id).textContent = count(value);
	}

	function renderStatus(status) {
		if (isTime(status.now))
			state.skew = status.now - Date.now() / 1000;
		$('m-protocol').textContent = status.protocol != null ? String(status.protocol) : '-';
		const catalog = status.catalog != null ? (typeof status.catalog === 'object' ? JSON.stringify(status.catalog) : String(status.catalog)) : '-';
		$('m-catalog').textContent = catalog.length > 16 ? catalog.slice(0, 16) + '...' : catalog;
		$('m-catalog').title = catalog;
		const up = isTime(status.started) && isTime(status.now);
		$('m-uptime').textContent = up ? duration(status.now - status.started) : '-';
		$('m-uptime').title = up ? 'Since ' + stamp(status.started) : '';

		const online = asArray(status.online);
		setCount('c-accounts', status.accounts);
		setCount('c-signed', status.signedIn);
		setCount('c-online', online.length);
		setCount('c-battles', status.battles);
		setCount('c-skirmish', status.skirmish);
		renderOnline(online);
		renderBoard(asArray(status.leaderboard));
		markOpenRows();
	}

	function showTable(name, rows, emptyText) {
		$(name + '-table').hidden = !rows.length;
		$(name + '-empty').hidden = rows.length > 0;
		$(name + '-empty').textContent = emptyText;
		$(name + '-rows').replaceChildren(...rows);
	}

	function accountCell(id) {
		return id ? h('a', { href: accountHref(id), title: String(id), class: 'mono' }, shortId(id).textContent) : dim('-');
	}

	function renderOnline(online) {
		$('online-count').textContent = online.length ? String(online.length) : '';
		const rows = online.map((entry) => {
			const player = asObject(entry);
			return h('tr', { class: player.account ? 'pick' : null, 'data-account': player.account || null, title: player.peer != null ? 'Peer ' + player.peer : null },
				h('td', { class: 'name' }, nameText(player.name)),
				h('td', null, accountCell(player.account)),
				h('td', null, providerBadge(player.provider)),
				h('td', null, placeBadge(player.where)));
		});
		showTable('online', rows, 'No one is connected.');
	}

	function renderBoard(board) {
		const rows = board.map((entry) => {
			const item = asObject(entry);
			const rank = isNum(item.rank) ? item.rank : 0;
			return h('tr', { class: item.account ? 'pick' : null, 'data-account': item.account || null },
				h('td', null, h('span', { class: 'rank chip-cut' + (rank >= 1 && rank <= 3 ? ' rank-' + rank : '') }, rank ? String(rank) : '-')),
				h('td', { class: 'name' }, item.account ? h('a', { href: accountHref(item.account) }, nameText(item.name)) : nameText(item.name)),
				h('td', { class: 'num' }, count(item.ring)),
				h('td', { class: 'when' }, when(item.at)),
				h('td', null, providerBadge(item.provider)));
		});
		showTable('board', rows, 'No one is ranked yet.');
	}

	// --- Accounts: search, filter, pages ---

	async function loadAccounts() {
		const list = state.list;
		const seq = ++list.seq;
		try {
			const answer = await request({ op: 'accounts', query: list.query, filter: list.filter, offset: list.offset, limit: PAGE_SIZE });
			if (seq !== list.seq)
				return;
			list.items = asArray(answer.accounts);
			list.total = isNum(answer.total) ? answer.total : list.items.length;
			if (isNum(answer.offset))
				list.offset = answer.offset;
			// A page emptied by a deletion: go back to the last one with accounts.
			if (!list.items.length && list.offset > 0 && list.total > 0) {
				list.offset = Math.floor((list.total - 1) / PAGE_SIZE) * PAGE_SIZE;
				loadAccounts();
				return;
			}
			$('accounts-error').textContent = '';
			renderAccounts();
		}
		catch (error) {
			if (seq === list.seq)
				$('accounts-error').textContent = 'Accounts: ' + describe(error);
		}
	}

	function accountRow(entry) {
		const item = asObject(entry);
		const id = String(item.account || '');
		return h('tr', { class: 'pick', 'data-account': id },
			h('td', { class: 'name' }, h('a', { href: accountHref(id) }, nameText(item.name))),
			h('td', null, shortId(id)),
			h('td', null, providerBadges(item.providers)),
			h('td', { class: 'num' }, count(item.deepest)),
			h('td', { class: 'num' }, count(item.battles)),
			h('td', { class: 'num' }, count(item.kills)),
			h('td', { class: 'when' }, when(item.created)),
			h('td', { class: 'when' }, when(item.updated)),
			h('td', null, flagBadges(flagsOf(item))));
	}

	function renderAccounts() {
		const { items, total, offset, query, filter } = state.list;
		$('accounts-total').textContent = whole.format(total);
		showTable('accounts', items.map(accountRow), query || filter !== 'all' ? 'No account matches.' : 'No accounts yet.');
		$('page-info').textContent = items.length ? whole.format(offset + 1) + '-' + whole.format(offset + items.length) + ' of ' + whole.format(total) : '';
		$('page-prev').disabled = offset <= 0;
		$('page-next').disabled = offset + items.length >= total;
		markOpenRows();
	}

	function markOpenRows() {
		for (const row of document.querySelectorAll('tr[data-account]'))
			row.classList.toggle('is-open', !!state.open && row.dataset.account === state.open);
	}

	function onRowClick(event) {
		if (event.target.closest('a, button, input, select'))
			return;
		const row = event.target.closest('tr[data-account]');
		if (row)
			location.hash = accountHref(row.dataset.account);
	}

	// --- The account panel ---

	function accountFromHash() {
		const match = /^#a=(.+)$/.exec(location.hash);
		if (!match)
			return '';
		try {
			return decodeURIComponent(match[1]);
		}
		catch (error) {
			return '';
		}
	}

	function route() {
		const id = accountFromHash();
		if (id)
			openAccount(id);
		else
			closeAccount();
	}

	function openAccount(id) {
		const drawer = $('drawer');
		if (id !== state.open) {
			state.open = id;
			state.answer = null;
			state.results = {};
			state.busy = {};
			$('drawer-name').textContent = 'Account';
			$('drawer-sub').replaceChildren(h('span', { class: 'mono' }, id));
			$('drawer-badges').replaceChildren();
			$('drawer-body').replaceChildren(h('p', { class: 'empty' }, state.connected ? 'Loading...' : 'Waiting for the connection...'));
			$('drawer-body').scrollTop = 0;
			loadAccount();
		}
		if (drawer.hidden) {
			drawer.hidden = false;
			$('drawer-name').focus({ preventScroll: true });
		}
		markOpenRows();
	}

	function closeAccount() {
		state.open = '';
		state.answer = null;
		$('drawer').hidden = true;
		markOpenRows();
	}

	function dismissAccount() {
		history.replaceState(null, '', location.pathname + location.search);
		closeAccount();
	}

	async function loadAccount() {
		const id = state.open;
		if (!id || !state.connected || state.loading === id)
			return;
		state.loading = id;
		try {
			await request({ op: 'account', account: id }); // receive() shows it
		}
		catch (error) {
			if (state.open !== id)
				return;
			if (state.answer)
				flash('Refreshing the account: ' + describe(error), true);
			else
				$('drawer-body').replaceChildren(h('p', { class: 'error' }, describe(error)));
		}
		finally {
			if (state.loading === id)
				state.loading = '';
		}
	}

	// A form someone is typing in is not refreshed under them.
	function isEditing() {
		const body = $('drawer-body');
		const active = document.activeElement;
		if (active && body.contains(active) && active.matches('input, select, textarea'))
			return true;
		return [...body.querySelectorAll('input')].some((input) => input.value !== input.defaultValue);
	}

	function refreshAccount() {
		if (state.open && !isEditing())
			loadAccount();
	}

	// Typed values and focus survive a re-render.
	function keepInputs(body) {
		const kept = new Map();
		for (const input of body.querySelectorAll('input[name]')) {
			if (input.value !== input.defaultValue)
				kept.set(input.name, input.value);
		}
		const active = document.activeElement;
		const focus = active && body.contains(active) ? active.getAttribute('name') : null;
		return { kept, focus };
	}

	function restoreInputs(body, { kept, focus }) {
		for (const input of body.querySelectorAll('input[name]')) {
			if (kept.has(input.name)) {
				input.value = kept.get(input.name);
				input.dispatchEvent(new Event('input', { bubbles: true }));
			}
		}
		if (focus) {
			const node = [...body.querySelectorAll('[name]')].find((el) => el.getAttribute('name') === focus);
			if (node && !node.disabled)
				node.focus({ preventScroll: true });
		}
	}

	function renderAccount(answer) {
		state.answer = answer;
		const data = asObject(answer.data);
		const flags = flagsOf(answer, data);
		const identities = asArray(data.identities).map(asObject);
		const name = $('drawer-name');
		name.textContent = data.name ? String(data.name) : '(no name)';
		name.classList.toggle('dim', !data.name);
		$('drawer-sub').replaceChildren(h('span', { class: 'mono' }, String(answer.account)), dim(', loaded ' + clock()));
		$('drawer-badges').replaceChildren(...providerList(identities), ...flagList(flags));

		const body = $('drawer-body');
		const kept = keepInputs(body);
		body.replaceChildren(
			summarySection(answer, data, flags, identities),
			colonySection(data),
			progressSection(data),
			actionsSection(answer, data, flags, identities),
			shipsSection(data),
			storageSection(data),
			rawSection(answer));
		restoreInputs(body, kept);
	}

	function summarySection(answer, data, flags, identities) {
		const signIns = identities.length
			? h('div', { class: 'ids' }, identities.map((identity) => h('div', null,
				providerBadge(identity.provider),
				h('span', { class: 'mono' }, String(identity.id ?? '')),
				identity.name ? dim(String(identity.name)) : null)))
			: dim('none: a guest');
		const notes = [flags.online ? 'connected' : 'offline'];
		if (flags.unsaved)
			notes.push('not saved (a connected guest, in memory only)');
		if (flags.kept)
			notes.push('kept (a guest saved by the admin, until it signs in)');
		if (flags.banned)
			notes.push('banned');
		if (flags.hidden)
			notes.push('hidden from the leaderboard');
		return section('Summary', null, h('dl', { class: 'kv' },
			kvRow('Id', h('span', { class: 'mono' }, String(answer.account))),
			kvRow('Sign-ins', signIns),
			kvRow('Devices', count(answer.devices), dim(' extra keys')),
			kvRow('Created', whenFull(data.created)),
			kvRow('Updated', whenFull(data.updated)),
			kvRow('State', notes.join(', '))));
	}

	function colonySection(data) {
		const resources = asObject(data.resources);
		const tiles = RESOURCES.map(([id, label]) => tile(count(resources[id]), label, 'r-' + id));
		tiles.push(tile(count(data.command), 'Command', 'r-command'));
		const levels = Object.entries(asObject(data.levels));
		return section('Colony', null,
			h('div', { class: 'tiles' }, tiles),
			levels.length ? h('div', { class: 'chips levels' }, levels.map(([id, level]) => h('span', { class: 'chip chip-cut' }, String(id), h('b', null, count(level))))) : null,
			...Object.entries(asObject(data.upgrades)).map(([id, done]) => h('p', { class: 'note' }, 'Upgrading ' + String(id) + ', done ', when(done))));
	}

	function progressSection(data) {
		const stats = asObject(data.stats);
		return section('Progress', null,
			h('div', { class: 'tiles three' },
				tile(count(data.deepest), 'Deepest ring'),
				tile(count(asArray(data.cleared).length), 'Cleared nodes'),
				tile(count(stats.battles), 'Battles'),
				tile(count(stats.won), 'Won'),
				tile(count(stats.lost), 'Ships lost'),
				tile(count(stats.kills), 'Kills')));
	}

	function shipsSection(data) {
		const ships = asArray(data.ships).map(asObject);
		return section('Ships', String(ships.length),
			ships.length ? h('div', { class: 'ships' }, ships.map(shipCard)) : h('p', { class: 'empty' }, 'No ships.'));
	}

	function shipCard(ship) {
		const slots = asArray(ship.slots).map(asObject);
		return h('div', { class: ship.away ? 'ship away' : 'ship' },
			h('div', { class: 'ship-head' },
				h('b', null, String(ship.hull || '?')),
				dim('#' + String(ship.id ?? '?')),
				conditionText(ship.condition),
				ship.away ? badge('away', 'w-battle') : null),
			h('div', { class: 'ship-row' }, h('span', null, 'Slots'),
				slots.length ? h('div', { class: 'chips' }, slots.map(slotChip)) : dim('none')),
			h('div', { class: 'ship-row' }, h('span', null, 'Hold'), stackChips(ship.hold, 'empty')));
	}

	function slotChip(slot) {
		if (!slot.id)
			return h('span', { class: 'chip chip-cut empty-slot' }, 'empty');
		return h('span', { class: 'chip chip-cut' }, String(slot.id), h('b', null, conditionText(slot.condition)));
	}

	function storageSection(data) {
		const modules = asArray(data.modules).map(asObject);
		const blueprints = asArray(data.blueprints);
		return section('Storage', null, h('dl', { class: 'kv' },
			kvRow('Items', stackChips(data.items, 'none')),
			kvRow('Modules', modules.length
				? h('div', { class: 'chips' }, modules.map((module) => h('span', { class: 'chip chip-cut' }, String(module.id), h('b', null, conditionText(module.condition)))))
				: dim('none')),
			kvRow('Blueprints', blueprints.length
				? h('div', { class: 'chips' }, blueprints.map((id) => h('span', { class: 'chip chip-cut' }, String(id))))
				: dim('none'))));
	}

	function rawSection(answer) {
		const text = JSON.stringify(answer, null, 2);
		const details = h('details', { class: 'raw', open: state.rawOpen },
			h('summary', null, 'Raw JSON'),
			h('div', { class: 'raw-tools' }, button('copy', 'Copy', 'small', () => copyText(text)), result('copy')),
			h('pre', null, text));
		details.addEventListener('toggle', () => {
			state.rawOpen = details.open;
		});
		return section('Raw', null, details);
	}

	async function copyText(text) {
		try {
			if (navigator.clipboard && window.isSecureContext)
				await navigator.clipboard.writeText(text);
			else
				copyFallback(text);
			setResult('copy', 'ok', 'Copied.');
		}
		catch (error) {
			setResult('copy', 'bad', 'Copy failed: select the text instead.');
		}
	}

	function copyFallback(text) {
		const area = h('textarea', { readonly: true, style: 'position:fixed;top:0;left:-9999px' });
		area.value = text;
		document.body.append(area);
		area.select();
		const ok = document.execCommand('copy');
		area.remove();
		if (!ok)
			throw new Error('copy');
	}

	// --- Actions: a small form each, its result beside it ---

	function button(key, label, cls, onClick) {
		const node = h('button', { class: 'btn' + (cls ? ' ' + cls : ''), type: onClick ? 'button' : 'submit', name: key }, label);
		if (onClick)
			node.addEventListener('click', onClick);
		node.disabled = !!state.busy[key];
		return node;
	}

	function result(key) {
		const node = h('span', { class: 'result', 'data-result': key, role: 'status' });
		paintResult(node, state.results[key]);
		return node;
	}

	function paintResult(node, value) {
		node.className = 'result' + (value ? ' ' + value.kind : '');
		node.textContent = value ? value.text : '';
	}

	function setResult(key, kind, text) {
		if (kind)
			state.results[key] = { kind, text };
		else
			delete state.results[key];
		for (const node of $('drawer-body').querySelectorAll('[data-result]')) {
			if (node.dataset.result === key)
				paintResult(node, state.results[key]);
		}
	}

	function setBusy(key, busy) {
		if (busy)
			state.busy[key] = true;
		else
			delete state.busy[key];
		for (const node of $('drawer-body').querySelectorAll('button[name]')) {
			if (node.getAttribute('name') === key)
				node.disabled = busy || (node.ready ? !node.ready() : false);
		}
	}

	function currentForm(key) {
		return [...$('drawer-body').querySelectorAll('form[data-action]')].find((form) => form.dataset.action === key);
	}

	// Sends an action on the open account; its answer (the account) re-renders the panel.
	async function act(key, message, options = {}) {
		if (state.busy[key])
			return;
		if (options.confirm && !window.confirm(options.confirm))
			return;
		const id = state.open;
		setBusy(key, true);
		setResult(key, 'busy', 'Working...');
		try {
			const answer = await request(Object.assign({}, message, { account: id }));
			if (state.open !== id)
				return;
			if (answer.op === 'deleted') {
				accountDeleted(id);
				return;
			}
			setResult(key, 'ok', (options.done || 'Done.') + ' ' + clock());
			const form = options.reset && currentForm(key);
			if (form)
				form.reset();
			loadAccounts();
			refreshStatus();
		}
		catch (error) {
			if (state.open === id)
				setResult(key, 'bad', describe(error));
		}
		finally {
			if (state.open === id)
				setBusy(key, false);
		}
	}

	function accountDeleted(id) {
		dismissAccount();
		flash('Deleted the account ' + id + '.');
		loadAccounts();
		refreshStatus();
	}

	function actionsSection(answer, data, flags, identities) {
		const label = data.name ? '"' + data.name + '"' : 'this account';
		return section('Actions', null, h('div', { class: 'acts' },
			grantForm(),
			renameForm(data.name ? String(data.name) : ''),
			accessBox(flags, label, answer.devices),
			identities.length ? signInsBox(identities, label) : keepBox(flags, label),
			deleteForm(String(answer.account), flags)));
	}

	function grantForm() {
		const fields = RESOURCES.map(([id, label]) => h('label', null, label,
			h('input', { class: 'field', type: 'number', name: 'grant-' + id, step: '1', placeholder: '0' })));
		const form = h('form', { class: 'act', 'data-action': 'grant' },
			h('div', { class: 'act-head' }, h('span', { class: 'label' }, 'Grant'), h('span', { class: 'note' }, 'Negative amounts take away, down to 0.')),
			h('div', { class: 'act-row' }, h('div', { class: 'grant-fields' }, fields), button('grant', 'Grant', 'primary')),
			result('grant'));
		form.addEventListener('submit', (event) => {
			event.preventDefault();
			const resources = {};
			let any = false;
			for (const [id] of RESOURCES) {
				const value = Number(form.elements['grant-' + id].value || 0);
				resources[id] = Number.isFinite(value) ? value : 0;
				any = any || resources[id] !== 0;
			}
			if (!any) {
				setResult('grant', 'bad', 'Enter an amount.');
				return;
			}
			act('grant', { op: 'grant', resources }, { done: 'Granted.', reset: true });
		});
		return form;
	}

	function renameForm(name) {
		const input = h('input', { class: 'field', name: 'rename-name', value: name, autocomplete: 'off', spellcheck: 'false', required: true });
		const form = h('form', { class: 'act', 'data-action': 'rename' },
			h('div', { class: 'act-row' },
				h('label', { class: 'grow' }, h('span', { class: 'label' }, 'Name'), input),
				button('rename', 'Rename')),
			result('rename'));
		form.addEventListener('submit', (event) => {
			event.preventDefault();
			const value = input.value.trim();
			if (!value) {
				setResult('rename', 'bad', 'Enter a name.');
				return;
			}
			if (value === name) {
				setResult('rename', 'bad', 'That is the name already.');
				return;
			}
			act('rename', { op: 'rename', name: value }, { done: 'Renamed.', reset: true });
		});
		return form;
	}

	function accessBox(flags, label, devices) {
		const ban = flags.banned
			? button('ban', 'Unban', '', () => act('ban', { op: 'ban', banned: false }, { done: 'Unbanned.' }))
			: button('ban', 'Ban', 'danger', () => act('ban', { op: 'ban', banned: true },
				{ confirm: 'Ban ' + label + '? It disconnects the player.', done: 'Banned.' }));
		const hide = flags.hidden
			? button('hide', 'Show on leaderboard', '', () => act('hide', { op: 'hide', hidden: false }, { done: 'Shown on the leaderboard.' }))
			: button('hide', 'Hide from leaderboard', '', () => act('hide', { op: 'hide', hidden: true }, { done: 'Hidden from the leaderboard.' }));
		const signout = button('signout', 'Sign out other devices', '', () => act('signout', { op: 'signout' },
			{ confirm: 'Sign out the other devices of ' + label + '? They must sign in again.', done: 'Other devices signed out.' }));
		return h('div', { class: 'act' },
			h('div', { class: 'act-head' }, h('span', { class: 'label' }, 'Access'),
				h('span', { class: 'note' }, count(devices) + ' extra device keys')),
			h('div', { class: 'act-row toggles' }, ban, hide, signout),
			result('ban'), result('hide'), result('signout'));
	}

	// A guest saved without a sign-in: its device's key opens it until a
	// sign-in joins it (a phone before its store's sign-in works).
	function keepBox(flags, label) {
		const keep = flags.kept
			? dim('Kept: saved, though it has no sign-in.')
			: button('keep', 'Keep this guest', '', () => act('keep', { op: 'keep' },
				{ confirm: 'Keep ' + label + '? Its device key opens it from now on, saved like a signed-in account.', done: 'Kept: saved.' }));
		return h('div', { class: 'act' },
			h('div', { class: 'act-head' }, h('span', { class: 'label' }, 'Guest'),
				h('span', { class: 'note' }, 'no sign-in')),
			h('div', { class: 'act-row toggles' }, keep),
			result('keep'));
	}

	function signInsBox(identities, label) {
		return h('div', { class: 'act' },
			h('div', { class: 'act-head' }, h('span', { class: 'label' }, 'Sign-ins')),
			identities.map((identity) => {
				const provider = String(identity.provider || '');
				const key = 'unlink:' + provider;
				return h('div', { class: 'ident' },
					h('div', { class: 'act-row' },
						providerBadge(provider),
						h('span', { class: 'mono grow' }, String(identity.id ?? '')),
						button(key, 'Unlink', 'small', () => act(key, { op: 'unlink', provider },
							{ confirm: 'Unlink ' + (PROVIDERS[provider] || provider) + ' from ' + label + '?', done: 'Unlinked.' }))),
					result(key));
			}));
	}

	function deleteForm(id, flags) {
		const input = h('input', { class: 'field mono', name: 'delete-confirm', autocomplete: 'off', spellcheck: 'false',
			placeholder: 'Type the id: ' + id, 'aria-label': 'Type the account id to delete it' });
		const go = button('delete', 'Delete', 'danger');
		go.ready = () => input.value.trim() === id;
		go.disabled = !!state.busy.delete || !go.ready();
		input.addEventListener('input', () => {
			go.disabled = !!state.busy.delete || !go.ready();
		});
		const form = h('form', { class: 'act danger', 'data-action': 'delete' },
			h('div', { class: 'act-head' }, h('span', { class: 'label' }, 'Delete'),
				h('span', { class: 'note' }, 'Removes the account and its sign-in links (the file moves to deleted/). Type its id to enable.')),
			h('div', { class: 'act-row' }, h('div', { class: 'grow' }, input), go),
			flags.online ? h('p', { class: 'note' }, 'The player is connected: the server refuses until they leave.') : null,
			result('delete'));
		form.addEventListener('submit', (event) => {
			event.preventDefault();
			if (!go.ready())
				return;
			act('delete', { op: 'delete' }, { done: 'Deleted.' });
		});
		return form;
	}

	// --- Start ---

	function bind() {
		for (const id of ['online-rows', 'board-rows', 'accounts-rows'])
			$(id).addEventListener('click', onRowClick);

		$('search').addEventListener('input', () => {
			clearTimeout(state.searchTimer);
			state.searchTimer = setTimeout(() => {
				const query = $('search').value.trim();
				if (query === state.list.query)
					return;
				state.list.query = query;
				state.list.offset = 0;
				loadAccounts();
			}, SEARCH_DELAY);
		});
		$('filter').addEventListener('change', () => {
			state.list.filter = $('filter').value;
			state.list.offset = 0;
			loadAccounts();
		});
		$('accounts-refresh').addEventListener('click', loadAccounts);
		$('page-prev').addEventListener('click', () => {
			state.list.offset = Math.max(0, state.list.offset - PAGE_SIZE);
			loadAccounts();
		});
		$('page-next').addEventListener('click', () => {
			state.list.offset += PAGE_SIZE;
			loadAccounts();
		});

		$('reconnect').addEventListener('click', connect);
		$('drawer-close').addEventListener('click', dismissAccount);
		$('drawer-refresh').addEventListener('click', loadAccount);
		window.addEventListener('hashchange', route);
		document.addEventListener('keydown', (event) => {
			if (event.key === 'Escape' && !$('drawer').hidden && !event.target.closest('input, select, textarea'))
				dismissAccount();
		});
		setInterval(tickTimes, TIMES_EVERY);
	}

	bind();
	route();
	connect();
})();
