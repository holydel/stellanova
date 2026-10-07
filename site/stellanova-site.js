// Stella Nova's site: reads the game's catalog, strings and leaderboard, and
// builds the blueprint cards and the leaderboard from them. No numbers of the
// game live here: they all come from stellanova-catalog.json.
'use strict';

(() => {
	const CATALOG_URL = 'stellanova-catalog.json';
	const STRINGS_URL = 'stellanova-strings-en.txt';
	const LEADERBOARD_URL = 'stellanova-data/leaderboard.json';
	const SPLASH_URL = 'stellanova-splash.jpg';
	const LEADERBOARD_SHOWN = 10;

	const DAMAGE_TYPES = ['em', 'explosive', 'kinetic', 'thermal'];
	const RESOURCES = ['metal', 'he3', 'chips'];
	const SVG_NS = 'http://www.w3.org/2000/svg';
	const DEG_PER_RAD = 180 / Math.PI;

	// Names the strings table has no key for.
	const KIND_NAMES = {
		laser: 'Laser',
		plasma: 'Plasma gun',
		shieldBooster: 'Shield booster',
		capacitorBattery: 'Capacitor battery',
	};
	const FACTION_NAMES = { colony: 'Colony', pirates: 'Pirates' };

	let strings = {};

	// --- Strings: "key = text" lines; '#' starts a comment. ---

	function parseStrings(text) {
		const table = {};
		for (const raw of text.split(/\r?\n/)) {
			const line = raw.trim();
			if (!line || line.startsWith('#'))
				continue;
			const eq = line.indexOf('=');
			if (eq <= 0)
				continue;
			table[line.slice(0, eq).trim()] = line.slice(eq + 1).trim();
		}
		return table;
	}

	// The text for a key, or the fallback; each {} takes the next argument.
	function t(key, fallback, ...args) {
		let text = Object.prototype.hasOwnProperty.call(strings, key) ? strings[key] : fallback;
		if (text == null)
			text = key;
		for (const arg of args)
			text = text.replace('{}', String(arg));
		return text;
	}

	function humanize(id) {
		const words = String(id).replace(/([a-z])([A-Z])/g, '$1 $2').replace(/_/g, ' ');
		return words.charAt(0).toUpperCase() + words.slice(1);
	}

	const hullName = (id) => t('hull.' + id, humanize(id));
	const moduleName = (id) => t('module.' + id, t('item.' + id, humanize(id)));
	const ammoName = (id) => t('ammo.' + id, humanize(id));
	const damageName = (type) => t('damage.' + type, humanize(type));
	const resourceName = (id) => t('resource.' + id, humanize(id));

	// --- DOM helpers. Text always goes in as text nodes, never as HTML. ---

	function h(tag, props, ...kids) {
		const node = document.createElement(tag);
		if (props) {
			for (const [key, value] of Object.entries(props)) {
				if (value == null || value === false)
					continue;
				if (key === 'class')
					node.className = value;
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

	function icon(name, cls) {
		const svg = document.createElementNS(SVG_NS, 'svg');
		svg.setAttribute('class', cls ? 'icon ' + cls : 'icon');
		svg.setAttribute('aria-hidden', 'true');
		svg.setAttribute('focusable', 'false');
		const use = document.createElementNS(SVG_NS, 'use');
		use.setAttribute('href', '#i-' + name);
		svg.append(use);
		return svg;
	}

	function svg(tag, attrs, ...kids) {
		const node = document.createElementNS(SVG_NS, tag);
		for (const [key, value] of Object.entries(attrs || {}))
			node.setAttribute(key, String(value));
		for (const kid of kids)
			node.append(kid);
		return node;
	}

	const isNum = (value) => typeof value === 'number' && Number.isFinite(value);

	function fmt(value, digits = 2) {
		return new Intl.NumberFormat('en-US', { maximumFractionDigits: digits }).format(value);
	}

	// A number with its unit: <b>400</b> HP. Catalog values keep their digits;
	// derived ones pass fewer.
	function qty(value, unit, digits = 4) {
		return [h('span', { class: 'num' }, fmt(value, digits)), unit ? h('span', { class: 'unit' }, unit) : null];
	}

	const sub = (text) => h('span', { class: 'sub' }, text);

	// One stat row: icon, label, value. opts: derived (worked out here; a
	// string says how), key (highlighted), wide (the value under the label, for
	// lists and bars), extra (a line under the label and value, such as damage
	// by type), hint (a tooltip for the row).
	function stat(iconName, name, value, opts = {}) {
		const cls = ['stat', opts.derived && 'derived', opts.key && 'key', opts.wide && 'wide'].filter(Boolean).join(' ');
		const how = typeof opts.derived === 'string' ? 'Derived: ' + opts.derived : 'Derived from the catalog';
		return h('div', { class: cls, title: opts.hint || null },
			icon(iconName),
			h('dt', null, name, opts.derived ? h('span', { class: 'calc', title: how }, 'derived') : null),
			h('dd', null, value),
			opts.extra ? h('div', { class: 'extra' }, opts.extra) : null);
	}

	function group(title, rows) {
		const list = rows.filter(Boolean);
		if (!list.length)
			return null;
		return h('section', { class: 'group' }, h('h5', null, title), h('dl', { class: 'stats' }, list));
	}

	// Damage amounts by type, as colored marks: {kinetic: 5, thermal: 6}.
	function damageList(amounts, notes) {
		const items = DAMAGE_TYPES.filter((type) => isNum(amounts[type]) && amounts[type] > 0).map((type) =>
			h('span', { class: 'dmg t-' + type },
				h('span', { class: 'dname' }, damageName(type)),
				h('span', { class: 'num' }, fmt(amounts[type])),
				notes && notes[type] ? sub(notes[type]) : null));
		return h('span', { class: 'dmg-list' }, items);
	}

	function sumDamage(amounts) {
		return DAMAGE_TYPES.reduce((total, type) => total + (isNum(amounts[type]) ? amounts[type] : 0), 0);
	}

	function resource(id, amount, suffix) {
		return h('span', { class: 'res res-' + id, title: resourceName(id) },
			icon(id), h('span', { class: 'sr' }, resourceName(id) + ' '), fmt(amount), suffix || null);
	}

	function costRow(cost, caption) {
		if (!cost)
			return null;
		const parts = RESOURCES.filter((id) => isNum(cost[id]) && cost[id] > 0).map((id) => resource(id, cost[id]));
		if (!parts.length)
			return null;
		return h('div', { class: 'cost-row' },
			h('span', { class: 'label' }, caption || 'Cost'),
			h('span', { class: 'costs' }, parts));
	}

	// --- Silhouettes: drawn shapes until the ships' design sheets exist. ---
	// A 200 x 120 drawing, nose to the right; `glows` are the engines.

	const SILHOUETTES = {
		lancer: {
			hull: 'M152 60 L128 55 L112 53 L104 44 L88 30 L76 30 L80 46 L66 48 L58 42 L46 42 L46 52 L52 54 L52 66 L46 68 L46 78 L58 78 L66 72 L80 74 L76 90 L88 90 L104 76 L112 67 L128 65 Z',
			lines: 'M136 60 L120 57 L114 60 L120 63 Z M112 60 L60 60 M100 47 L84 34 M100 73 L84 86',
			glows: [[41, 43, 5, 8], [41, 69, 5, 8]],
		},
		raider: {
			hull: 'M150 60 L126 52 L132 34 L118 46 L100 46 L84 26 L72 28 L80 48 L60 50 L50 44 L44 52 L44 68 L50 76 L60 70 L80 72 L72 92 L84 94 L100 74 L118 74 L132 86 L126 68 Z',
			lines: 'M130 60 L114 56 L108 60 L114 64 Z M108 60 L56 60 M96 50 L82 32 M96 70 L82 88',
			glows: [[39, 54, 5, 12]],
		},
		other: {
			hull: 'M150 60 L96 36 L66 40 L52 60 L66 80 L96 84 Z',
			lines: 'M150 60 L58 60',
			glows: [[47, 55, 5, 10]],
		},
	};

	function blueprint(hull) {
		const shape = SILHOUETTES[hull.id] || SILHOUETTES.other;
		const art = svg('svg', { viewBox: '0 0 200 120', role: 'img', 'aria-label': hullName(hull.id) + ', drawn from above' });
		art.append(svg('circle', { class: 'bp-ring', cx: 98, cy: 60, r: 54 }));
		for (const [x, y, w, hgt] of shape.glows)
			art.append(svg('rect', { class: 'bp-glow', x, y, width: w, height: hgt, rx: 1 }));
		art.append(svg('path', { class: 'bp-hull', d: shape.hull }));
		art.append(svg('path', { class: 'bp-line', d: shape.lines }));
		const tag = svg('text', { class: 'bp-text', x: 8, y: 14 });
		tag.textContent = (hull.id + ' / ' + (hull.size || '')).toUpperCase();
		art.append(tag);
		const caption = isNum(hull.radius) ? 'Dashed: the hit circle, radius ' + fmt(hull.radius) + ' m. Not to scale.' : 'Not to scale.';
		return h('figure', { class: 'blueprint' }, art, h('figcaption', null, caption));
	}

	// --- Cards ---

	function hullCard(hull, catalog) {
		const enemy = hull.faction !== 'colony';

		const flight = [];
		if (isNum(hull.mass))
			flight.push(stat('mass', 'Mass', qty(hull.mass, 't')));
		if (isNum(hull.maxSpeed))
			flight.push(stat('speed', 'Top speed', qty(hull.maxSpeed, 'm/s')));
		if (isNum(hull.thrustForward) && isNum(hull.thrustBackward))
			flight.push(stat('thrust', 'Thrust', [h('span', { class: 'num' }, fmt(hull.thrustForward) + ' / ' + fmt(hull.thrustBackward)), h('span', { class: 'unit' }, 'kN'), sub('forward / back')]));
		if (isNum(hull.thrustForward) && isNum(hull.mass) && hull.mass > 0) {
			const forward = hull.thrustForward / hull.mass;
			const back = isNum(hull.thrustBackward) ? hull.thrustBackward / hull.mass : null;
			flight.push(stat('accel', 'Acceleration',
				[h('span', { class: 'num' }, fmt(forward, 1) + (back != null ? ' / ' + fmt(back, 1) : '')), h('span', { class: 'unit' }, 'm/s²'), sub('empty hull')],
				{ derived: 'thrust ÷ mass; modules and cargo add mass' }));
		}
		if (isNum(hull.thrustTurn))
			flight.push(stat('turn', 'Turn thrust', qty(hull.thrustTurn, 'kN')));
		if (isNum(hull.thrustTurn) && isNum(hull.mass) && hull.mass > 0) {
			const rad = hull.thrustTurn / hull.mass;
			flight.push(stat('turn', 'Turn rate',
				[...qty(rad * DEG_PER_RAD, '°/s', 0), sub(fmt(rad) + ' rad/s, empty')],
				{ derived: 'turn thrust ÷ mass, in rad/s' }));
		}

		const defense = [];
		if (isNum(hull.hull))
			defense.push(stat('hull', 'Hull', qty(hull.hull, 'HP')));
		if (isNum(hull.shield))
			defense.push(stat('shield', 'Shield', qty(hull.shield, 'HP')));
		if (isNum(hull.shieldRegen))
			defense.push(stat('regen', 'Shield regen', qty(hull.shieldRegen, 'HP/s')));
		if (isNum(hull.shieldThreshold))
			defense.push(stat('threshold', 'Shield threshold', [...qty(hull.shieldThreshold, 'HP'), sub('a hit')],
				{ hint: 'The most kinetic and thermal damage the shield takes from one hit' }));
		if (hull.resist) {
			const bars = DAMAGE_TYPES.map((type) => {
				const value = isNum(hull.resist[type]) ? hull.resist[type] : 0;
				const bar = h('span', { class: 'rbar' }, h('i'));
				bar.style.setProperty('--v', Math.max(0, Math.min(1, value)) * 100 + '%');
				return h('span', { class: 'resist t-' + type },
					h('span', { class: 'rname' }, damageName(type)), bar,
					h('span', { class: 'rval' }, fmt(value * 100, 0) + '%'));
			});
			defense.push(stat('resist', 'Resistances', h('span', { class: 'resists' }, bars), { wide: true }));
		}

		const power = [];
		if (isNum(hull.reactor))
			power.push(stat('power', 'Reactor', qty(hull.reactor, 'MW')));
		if (isNum(hull.capacitor))
			power.push(stat('capacitor', 'Capacitor', qty(hull.capacitor, 'GJ')));
		if (isNum(hull.reactor) && hull.reactor > 0) {
			const perSecond = hull.reactor / 1000;
			const full = isNum(hull.capacitor) ? hull.capacitor / perSecond : null;
			power.push(stat('energy', 'Capacitor at rest',
				[...qty(perSecond, 'GJ/s', 3), full != null ? sub('full in ' + fmt(full, 0) + ' s') : null],
				{ derived: 'reactor ÷ 1000, with no modules drawing power' }));
		}

		const fitting = [];
		if (hull.slots) {
			const kinds = ['external', 'internal', 'rig'];
			const boxes = kinds.filter((kind) => isNum(hull.slots[kind])).map((kind) =>
				h('span', { class: hull.slots[kind] > 0 ? 'slot' : 'slot none' }, t('slot.' + kind, humanize(kind)), h('b', null, fmt(hull.slots[kind]))));
			fitting.push(stat('slots', 'Slots', h('span', { class: 'slots' }, boxes), { wide: true }));
		}
		if (isNum(hull.scanner))
			fitting.push(stat('scanner', 'Scanner', qty(hull.scanner, 'm'), { hint: 'How far the ship sees: its turrets pick targets within it' }));
		if (isNum(hull.cargo))
			fitting.push(stat('cargo', 'Cargo hold', qty(hull.cargo, 'm³')));
		if (isNum(hull.cargoMassFactor))
			fitting.push(stat('mass', 'Cargo mass factor', h('span', { class: 'num' }, '×' + fmt(hull.cargoMassFactor)),
				{ hint: 'Cargo mass times this is added to the ship\'s mass' }));

		const groups = [group('Flight', flight), group('Defense', defense), group('Power', power), group('Fitting', fitting)];

		if (enemy && catalog.enemies) {
			const enemies = catalog.enemies;
			const fits = (enemies.fits || []).filter((fit) => fit.hull === hull.id);
			const armed = [];
			if (fits.length) {
				const names = fits.map((fit) => (fit.modules || []).map(moduleName).join(' + ') || 'nothing').join(', or ');
				armed.push(stat('damage', 'Armed with', h('span', null, names), { wide: true }));
			}
			if (isNum(enemies.growth))
				armed.push(stat('grow', 'Each ring outward', [h('span', { class: 'num' }, '×' + fmt(enemies.growth)), sub('hull, shield and damage')]));
			groups.push(group('As an enemy', armed));
		}

		const tags = [h('span', { class: 'tag' }, t('class.' + hull.class, humanize(hull.class || 'hull')))];
		if (hull.size)
			tags.push(h('span', { class: 'tag' }, 'Size ' + hull.size));
		const faction = FACTION_NAMES[hull.faction] || humanize(hull.faction || 'unknown');
		tags.push(h('span', { class: enemy ? 'tag enemy' : 'tag ours' }, enemy ? 'Enemy: ' + faction : faction));

		// The numbers to see first, beside the drawing.
		const headline = [
			['hull', 'Hull', hull.hull, 'HP'],
			['shield', 'Shield', hull.shield, 'HP'],
			['speed', 'Top speed', hull.maxSpeed, 'm/s'],
			['power', 'Reactor', hull.reactor, 'MW'],
		].filter((item) => isNum(item[2])).map(([iconName, name, value, unit]) =>
			h('div', { class: 'hl' }, icon(iconName),
				h('span', { class: 'hl-value' }, h('span', { class: 'num' }, fmt(value)), h('span', { class: 'unit' }, unit)),
				h('span', { class: 'hl-name' }, name)));

		return h('article', { class: 'card cut hull-card' + (enemy ? ' enemy' : ''), 'data-faction': hull.faction || '' },
			h('div', { class: 'hull-top' },
				blueprint(hull),
				h('div', { class: 'hull-info' },
					h('h4', null, hullName(hull.id)),
					h('div', { class: 'tags' }, tags),
					h('div', { class: 'headline' }, headline),
					costRow(hull.cost, 'Build cost'))),
			h('div', { class: 'groups hull-groups' }, groups));
	}

	// A plasma gun's shot: its ammo's damage times the gun's factors.
	function shotDamage(module, ammo) {
		const amounts = {};
		const notes = {};
		if (!ammo || !ammo.damage)
			return { amounts, notes };
		for (const type of DAMAGE_TYPES) {
			const base = ammo.damage[type];
			if (!isNum(base) || base <= 0)
				continue;
			const factor = module.factor && isNum(module.factor[type]) ? module.factor[type] : 1;
			amounts[type] = base * factor;
			notes[type] = fmt(base) + ' × ' + fmt(factor, 2);
		}
		return { amounts, notes };
	}

	// Shots in a magazine, as the game counts them (sim/src/fitting.cpp).
	function magazineShots(module, ammo) {
		if (!ammo || !isNum(module.magazine) || !isNum(ammo.volume) || ammo.volume <= 0)
			return null;
		return Math.floor(module.magazine / ammo.volume + 1e-3);
	}

	function moduleCard(module, catalog, rules) {
		const ammo = module.ammo ? (catalog.ammo || []).find((a) => a.id === module.ammo) : null;

		const fitting = [];
		if (isNum(module.health))
			fitting.push(stat('hull', 'Hit points', qty(module.health, 'HP')));
		if (isNum(module.mass))
			fitting.push(stat('mass', 'Mass', qty(module.mass, 't')));
		if (isNum(module.volume))
			fitting.push(stat('cargo', 'Volume', qty(module.volume, 'm³')));
		if (isNum(module.power))
			fitting.push(stat('power', 'Power', qty(module.power, 'MW'), { hint: 'Drawn from the reactor while fitted' }));
		if (isNum(module.energy) && module.energy > 0) {
			if (module.kind === 'plasma')
				fitting.push(stat('energy', 'Energy', [...qty(module.energy, 'GJ'), sub('a shot')]));
			else
				fitting.push(stat('energy', 'Energy', [...qty(module.energy, 'GJ/s', 3), sub(module.kind === 'laser' ? 'while firing' : 'while on')]));
		}

		const action = [];
		let actionTitle = 'Weapon';
		if (isNum(module.range))
			action.push(stat('range', 'Range', qty(module.range, 'm')));
		if (isNum(module.turnSpeed))
			action.push(stat('turn', 'Turret turn', [...qty(module.turnSpeed * DEG_PER_RAD, '°/s', 0), sub(fmt(module.turnSpeed) + ' rad/s')]));

		if (module.kind === 'laser' && module.dps) {
			action.push(stat('damage', 'Damage a second', qty(sumDamage(module.dps), 'DPS', 1),
				{ key: true, extra: damageList(module.dps), hint: 'A beam: it hits once a tick and needs no ammo' }));
		}
		else if (module.kind === 'plasma') {
			if (isNum(module.fireRate))
				action.push(stat('firerate', 'Fire rate', qty(module.fireRate, 'shots/s')));
			const shots = magazineShots(module, ammo);
			if (isNum(module.magazine))
				action.push(stat('magazine', 'Magazine', [...qty(module.magazine, 'm³', 3), shots != null ? sub(fmt(shots, 0) + ' shots') : null]));
			if (isNum(module.reload))
				action.push(stat('reload', 'Reload', qty(module.reload, 's')));
			if (isNum(module.shotSpeed))
				action.push(stat('shot', 'Shot speed', qty(module.shotSpeed, 'm/s')));
			if (ammo)
				action.push(stat('ammo', 'Ammo', h('span', { class: 'text' }, ammoName(ammo.id))));
			const shot = shotDamage(module, ammo);
			const perShot = sumDamage(shot.amounts);
			if (perShot > 0) {
				action.push(stat('damage', 'Damage a shot', qty(perShot, null, 2),
					{ extra: damageList(shot.amounts, shot.notes), derived: 'the ammo\'s damage × the gun\'s factor, per type' }));
				if (isNum(module.fireRate)) {
					action.push(stat('damage', 'Damage a second',
						[...qty(perShot * module.fireRate, 'DPS', 1), sub(fmt(perShot) + ' × ' + fmt(module.fireRate) + ' shots/s')],
						{ key: true, derived: 'damage a shot × fire rate' }));
					if (shots && isNum(module.reload)) {
						const cycle = shots / module.fireRate + module.reload;
						action.push(stat('reload', 'With reloads',
							[...qty(shots * perShot / cycle, 'DPS', 0), sub(fmt(shots, 0) + ' shots + ' + fmt(module.reload) + ' s')],
							{ derived: 'a magazine\'s damage ÷ (its firing time + reload), about' }));
					}
				}
			}
		}
		else if (module.kind === 'shieldBooster') {
			actionTitle = 'Effect';
			if (isNum(module.boost))
				action.push(stat('regen', 'Shield boost', qty(module.boost, 'HP/s', 1), { key: true }));
			if (isNum(module.boost) && isNum(module.energy) && module.energy > 0)
				action.push(stat('energy', 'Shield per energy', qty(module.boost / module.energy, 'HP/GJ', 0), { derived: 'shield boost ÷ energy' }));
		}
		else if (module.kind === 'capacitorBattery') {
			actionTitle = 'Effect';
			if (isNum(module.capacitor))
				action.push(stat('capacitor', 'Capacitor', [h('span', { class: 'num' }, '+' + fmt(module.capacitor)), h('span', { class: 'unit' }, 'GJ')], { key: true }));
		}

		let note = null;
		if (module.kind === 'shieldBooster' && rules && isNum(rules.boosterCapacitor))
			note = 'On while the shield is below full and the capacitor above ' + fmt(rules.boosterCapacitor * 100, 0) + '%.';
		else if (module.kind === 'laser' && rules && isNum(rules.beamAim))
			note = 'Fires once the turret is within ' + fmt(rules.beamAim) + '° of its target.';
		else if (module.kind === 'plasma' && rules && isNum(rules.shotAim))
			note = 'Leads its target, and fires within ' + fmt(rules.shotAim) + '° of the lead point.';

		const tags = [h('span', { class: 'tag' }, KIND_NAMES[module.kind] || humanize(module.kind || 'module'))];
		if (module.slot)
			tags.push(h('span', { class: 'tag' }, t('slot.' + module.slot, humanize(module.slot))));
		if (module.size)
			tags.push(h('span', { class: 'tag' }, 'Size ' + module.size));

		return h('article', { class: 'card cut module-card' },
			h('header', { class: 'card-head' }, h('div', null, h('h4', null, moduleName(module.id)), h('div', { class: 'tags' }, tags))),
			group(actionTitle, action),
			note ? h('p', { class: 'note-line' }, note) : null,
			group('Fitting', fitting),
			h('div', { class: 'grow' }),
			costRow(module.cost, 'Build cost'));
	}

	function ammoCard(ammo, catalog) {
		const rows = [];
		if (isNum(ammo.volume))
			rows.push(stat('cargo', 'Volume', qty(ammo.volume, 'm³', 3)));
		if (isNum(ammo.mass))
			rows.push(stat('mass', 'Mass', qty(ammo.mass, 't', 3)));
		if (ammo.damage) {
			rows.push(stat('damage', 'Damage a shot', qty(sumDamage(ammo.damage)),
				{ extra: damageList(ammo.damage), hint: 'Before the gun\'s factors' }));
		}
		const users = (catalog.modules || []).filter((module) => module.ammo === ammo.id).map((module) => moduleName(module.id));
		if (users.length)
			rows.push(stat('firerate', 'Used by', h('span', { class: 'text' }, users.join(', '))));
		const batch = isNum(ammo.batch) ? ammo.batch : 1;
		rows.push(stat('batch', 'Built', [h('span', { class: 'num' }, fmt(batch)), h('span', { class: 'unit' }, 'at a time')]));

		return h('article', { class: 'card cut ammo-card' },
			h('header', { class: 'card-head' }, h('div', null, h('h4', null, ammoName(ammo.id)), h('div', { class: 'tags' }, h('span', { class: 'tag' }, 'Ammo')))),
			group('Charge', rows),
			h('div', { class: 'grow' }),
			costRow(ammo.cost, batch > 1 ? 'Cost for ' + fmt(batch) : 'Build cost'));
	}

	// --- Damage examples: the game's rule (sim/src/world.cpp, SplitDamage). ---

	function splitDamage(resist, threshold, shield, damage) {
		const taken = {};
		for (const type of DAMAGE_TYPES) {
			const amount = Math.max(0, isNum(damage[type]) ? damage[type] : 0);
			const cut = Math.min(1, Math.max(0, 1 - (resist && isNum(resist[type]) ? resist[type] : 0)));
			taken[type] = amount * cut;
		}
		const soft = taken.em + taken.explosive;
		const hard = taken.kinetic + taken.thermal;
		shield = Math.max(0, shield);
		const toShieldSoft = Math.min(shield, soft);
		const stopped = Math.min(shield - toShieldSoft, Math.min(hard, Math.max(0, threshold)));
		return {
			shield: toShieldSoft + stopped,
			hull: soft - toShieldSoft + hard - stopped,
			resisted: sumDamage(damage) - soft - hard,
		};
	}

	function exampleRow(title, detail, split, scale) {
		const pct = (value) => (value / scale) * 100 + '%';
		const bar = h('div', { class: 'ex-bar', role: 'presentation' });
		for (const [cls, value] of [['to-shield', split.shield], ['to-hull', split.hull], ['resisted', split.resisted]]) {
			if (value > 1e-6) {
				const part = h('span', { class: cls });
				part.style.width = pct(value);
				bar.append(part);
			}
		}
		const parts = [h('span', { class: 'k-shield' }, 'Shield ', h('b', null, fmt(split.shield, 1)))];
		parts.push(h('span', { class: 'k-hull' }, 'Hull ', h('b', null, fmt(split.hull, 1))));
		if (split.resisted > 1e-6)
			parts.push(h('span', { class: 'k-resist' }, 'Resisted ', h('b', null, fmt(split.resisted, 1))));
		return h('div', { class: 'example' },
			h('p', { class: 'ex-label' }, title, detail ? h('span', { class: 'sub' }, ' ' + detail) : null),
			bar,
			h('p', { class: 'ex-parts' }, parts));
	}

	function renderDamageExamples(catalog) {
		const box = document.getElementById('damage-examples');
		const rows = [];
		// The design's own example: threshold 10, hits of 30 and 6, full shield.
		const threshold = 10;
		const examples = [
			{ type: 'em', amount: 30 },
			{ type: 'kinetic', amount: 30 },
			{ type: 'kinetic', amount: 6 },
		];
		let scale = 30;
		const live = [];
		// And one from the catalog: a plasma shot against the first hull of ours.
		const hull = (catalog.hulls || []).find((x) => x.faction === 'colony');
		const gun = (catalog.modules || []).find((m) => m.kind === 'plasma' && m.ammo);
		const ammo = gun ? (catalog.ammo || []).find((a) => a.id === gun.ammo) : null;
		if (hull && gun && ammo) {
			const shot = shotDamage(gun, ammo).amounts;
			const total = sumDamage(shot);
			if (total > 0) {
				live.push({ hull, gun, shot, total });
				scale = Math.max(scale, total);
			}
		}
		for (const ex of examples) {
			const split = splitDamage(null, threshold, Infinity, { [ex.type]: ex.amount });
			rows.push(exampleRow(damageName(ex.type) + ' hit of ' + fmt(ex.amount) + ',', 'threshold ' + fmt(threshold), split, scale));
		}
		for (const ex of live) {
			const split = splitDamage(ex.hull.resist, ex.hull.shieldThreshold || 0, ex.hull.shield || 0, ex.shot);
			rows.push(exampleRow(moduleName(ex.gun.id) + ' shot of ' + fmt(ex.total, 1) + ' on a ' + hullName(ex.hull.id) + ',',
				'its resistances, then threshold ' + fmt(ex.hull.shieldThreshold || 0) + ' (from the catalog)', split, scale));
		}
		box.replaceChildren(...rows);
	}

	// --- How it plays: a line of numbers under each step. ---

	function minutes(seconds) {
		return seconds % 60 === 0 ? fmt(seconds / 60) + ' min' : fmt(seconds) + ' s';
	}

	function fillFacts(catalog) {
		const fact = (name) => document.querySelector('[data-fact="' + name + '"]');
		const rules = catalog.rules || {};
		const colony = catalog.colony || {};
		const map = catalog.map || {};
		const buildings = colony.buildings || [];

		const producers = buildings.filter((b) => b.produces && isNum(b.rate));
		const command = buildings.find((b) => isNum(b.points));
		const colonyFact = fact('colony');
		if (colonyFact && producers.length) {
			colonyFact.append('At level 1, an hour: ');
			for (const b of producers)
				colonyFact.append(resource(b.produces, b.rate));
			if (command && isNum(command.refill))
				colonyFact.append(h('br'), fmt(command.points) + ' command points, one back every ' + minutes(command.refill) + '.');
		}

		const enemies = catalog.enemies || {};
		const mapFact = fact('map');
		if (mapFact && isNum(enemies.base) && isNum(enemies.perRings) && enemies.perRings > 0) {
			const count = (ring) => Math.min(enemies.max || Infinity, enemies.base + Math.floor(ring / enemies.perRings));
			let text = 'Enemies: ' + count(1) + ' in ring 1, ' + count(5) + ' in ring 5, ' + count(10) + ' in ring 10';
			if (isNum(enemies.max))
				text += ', ' + fmt(enemies.max) + ' at most';
			text += '.';
			if (isNum(enemies.growth))
				text += ' Each ring makes them ' + fmt((enemies.growth - 1) * 100, 0) + '% stronger.';
			if (isNum(map.fog))
				text += ' You see ' + fmt(map.fog) + ' rings past explored space.';
			mapFact.textContent = text;
		}

		const launchFact = fact('launch');
		const launch = map.launch || {};
		if (launchFact && isNum(launch.command) && isNum(launch.fuel)) {
			let text = 'Costs ' + fmt(launch.command) + ' command point' + (launch.command === 1 ? '' : 's') +
				', and He-3 for each ship: ' + fmt(launch.fuel) + (isNum(launch.fuelPerRing) ? ' + ' + fmt(launch.fuelPerRing) + ' per ring' : '') + '.';
			if (isNum(rules.maxFleet))
				text += ' Up to ' + fmt(rules.maxFleet) + ' ships.';
			launchFact.textContent = text;
		}

		const battleFact = fact('battle');
		if (battleFact && isNum(rules.attackRange))
			battleFact.textContent = 'Attacking ships close to ' + fmt(rules.attackRange * 100, 0) + '% of their weapon range and circle the target there.';

		const lootFact = fact('loot');
		if (lootFact && isNum(rules.crateLife) && isNum(rules.pickupRange))
			lootFact.textContent = 'Crates last ' + fmt(rules.crateLife) + ' s. A ship picks one up within ' + fmt(rules.pickupRange) + ' m.';

		const growFact = fact('grow');
		const growth = producers.length && isNum(producers[0].growth) ? producers[0].growth : null;
		if (growFact && (growth || isNum(rules.repairMetal))) {
			const parts = [];
			if (growth)
				parts.push('Production grows ×' + fmt(growth) + ' a level.');
			if (isNum(rules.repairMetal))
				parts.push('Repairs cost ' + fmt(rules.repairMetal) + ' metal a hit point.');
			growFact.textContent = parts.join(' ');
		}

		const moduleFact = fact('moduleDamage');
		if (moduleFact && isNum(rules.moduleDamage))
			moduleFact.textContent = ', by ' + fmt(rules.moduleDamage * 100, 0) + '% of the hull damage';
	}

	// --- The catalog ---

	function renderCatalog(catalog) {
		const rules = catalog.rules || {};
		const hulls = catalog.hulls || [];
		const hullBox = document.getElementById('hulls');
		const hullCards = hulls.map((hull) => hullCard(hull, catalog));
		hullBox.replaceChildren(...hullCards);

		const toggle = document.getElementById('show-enemies');
		const enemyCards = hullCards.filter((card) => card.classList.contains('enemy'));
		if (enemyCards.length) {
			document.getElementById('enemy-toggle').hidden = false;
			const apply = () => enemyCards.forEach((card) => { card.hidden = !toggle.checked; });
			toggle.addEventListener('change', apply);
			apply();
		}

		document.getElementById('modules').replaceChildren(...(catalog.modules || []).map((module) => moduleCard(module, catalog, rules)));
		document.getElementById('ammo').replaceChildren(...(catalog.ammo || []).map((ammo) => ammoCard(ammo, catalog)));
		renderDamageExamples(catalog);
		fillFacts(catalog);
	}

	// --- The leaderboard ---

	const dateFormat = new Intl.DateTimeFormat('en-GB', { day: 'numeric', month: 'short', year: 'numeric' });
	const timeFormat = new Intl.DateTimeFormat('en-GB', { day: 'numeric', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit' });

	async function loadLeaderboard() {
		const table = document.getElementById('board-table');
		const body = document.getElementById('board-rows');
		const empty = document.getElementById('board-empty');
		const emptyText = document.getElementById('board-empty-text');
		const meta = document.getElementById('board-meta');
		let data = null;
		try {
			const response = await fetch(LEADERBOARD_URL, { cache: 'no-cache' });
			if (response.ok)
				data = await response.json();
		}
		catch (error) {
			data = null;
		}
		const entries = data && Array.isArray(data.entries)
			? data.entries.filter((e) => e && typeof e.name === 'string' && isNum(e.ring) && e.ring > 0).slice(0, LEADERBOARD_SHOWN)
			: [];
		if (!entries.length) {
			table.hidden = true;
			empty.hidden = false;
			emptyText.textContent = 'No one has cleared a ring yet: be the first.';
			return;
		}
		const rows = entries.map((entry, i) => {
			const rank = isNum(entry.rank) ? entry.rank : i + 1;
			return h('tr', null,
				h('td', null, h('span', { class: 'rank rank-' + rank }, String(rank))),
				h('td', { class: 'name', title: entry.name }, entry.name),
				h('td', null, h('span', { class: 'ring' }, icon('map'), fmt(entry.ring))),
				h('td', { class: 'c-date' }, isNum(entry.at) ? dateFormat.format(new Date(entry.at * 1000)) : ''));
		});
		body.replaceChildren(...rows);
		table.hidden = false;
		empty.hidden = true;
		const parts = [];
		if (isNum(data.season))
			parts.push('Season ' + data.season);
		parts.push('top ' + LEADERBOARD_SHOWN);
		if (isNum(data.updated))
			parts.push('updated ' + timeFormat.format(new Date(data.updated * 1000)));
		meta.textContent = parts.join(' · ');
	}

	// --- Strings in the page: [data-str] elements, and the footer's credits. ---

	function applyStrings() {
		for (const node of document.querySelectorAll('[data-str]')) {
			const key = node.getAttribute('data-str');
			if (Object.prototype.hasOwnProperty.call(strings, key))
				node.textContent = strings[key];
		}
		const credits = document.getElementById('credits');
		const items = ['credits.sky', 'credits.sounds', 'credits.fonts']
			.filter((key) => strings[key])
			.map((key) => h('li', null, strings[key]));
		if (items.length)
			credits.replaceChildren(h('li', null, 'In the game:'), ...items);
	}

	// --- Looks: the starfield, the splash, the WebGPU note. ---

	function drawStars() {
		const canvas = document.getElementById('stars');
		const context = canvas.getContext && canvas.getContext('2d');
		if (!context)
			return;
		let drawnWidth = 0;
		let drawnHeight = 0;
		const draw = () => {
			const width = window.innerWidth;
			const height = window.innerHeight;
			// Phones resize the view as their address bar slides: redraw only for real changes.
			if (width === drawnWidth && Math.abs(height - drawnHeight) < 120)
				return;
			drawnWidth = width;
			drawnHeight = height;
			const ratio = Math.min(window.devicePixelRatio || 1, 2);
			canvas.width = Math.round(width * ratio);
			canvas.height = Math.round(height * ratio);
			context.setTransform(ratio, 0, 0, ratio, 0, 0);
			context.clearRect(0, 0, width, height);
			let seed = 20261004;
			const random = () => {
				seed = (seed + 0x6d2b79f5) | 0;
				let x = Math.imul(seed ^ (seed >>> 15), 1 | seed);
				x = (x + Math.imul(x ^ (x >>> 7), 61 | x)) ^ x;
				return ((x ^ (x >>> 14)) >>> 0) / 4294967296;
			};
			const count = Math.round((width * height) / 2600);
			for (let i = 0; i < count; ++i) {
				const x = random() * width;
				const y = random() * height;
				const bright = random();
				const size = bright > 0.985 ? 1.6 : bright > 0.9 ? 1.1 : 0.7;
				const tint = random();
				const color = tint > 0.8 ? '172, 219, 233' : tint > 0.7 ? '255, 230, 200' : '230, 241, 255';
				context.fillStyle = 'rgba(' + color + ', ' + (0.25 + bright * 0.7).toFixed(2) + ')';
				context.beginPath();
				context.arc(x, y, size, 0, Math.PI * 2);
				context.fill();
			}
		};
		draw();
		let timer = 0;
		window.addEventListener('resize', () => {
			clearTimeout(timer);
			timer = setTimeout(draw, 150);
		});
	}

	function loadSplash() {
		const hero = document.querySelector('.hero');
		const image = new Image();
		image.onload = () => hero.classList.add('has-splash');
		image.src = SPLASH_URL;
	}

	function checkWebGpu() {
		if (!('gpu' in navigator))
			document.getElementById('no-webgpu').hidden = false;
	}

	// --- Start ---

	async function fetchText(url) {
		const response = await fetch(url, { cache: 'no-cache' });
		if (!response.ok)
			throw new Error(url + ': ' + response.status);
		return response.text();
	}

	async function start() {
		drawStars();
		loadSplash();
		checkWebGpu();
		const board = loadLeaderboard();

		const [catalogText, stringsText] = await Promise.allSettled([fetchText(CATALOG_URL), fetchText(STRINGS_URL)]);
		if (stringsText.status === 'fulfilled')
			strings = parseStrings(stringsText.value);
		applyStrings();

		let catalog = null;
		try {
			if (catalogText.status === 'fulfilled')
				catalog = JSON.parse(catalogText.value);
		}
		catch (error) {
			catalog = null;
		}
		try {
			if (!catalog)
				throw new Error(CATALOG_URL + ' did not load');
			renderCatalog(catalog);
			document.getElementById('blueprints').hidden = false;
		}
		catch (error) {
			console.error(error);
			document.getElementById('blueprints').hidden = true;
			document.getElementById('catalog-error').hidden = false;
		}
		await board;
	}

	start();
})();
