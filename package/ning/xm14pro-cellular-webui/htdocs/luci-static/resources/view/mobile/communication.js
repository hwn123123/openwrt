'use strict';
'require view';
'require poll';
'require mobile.common as mobile';

function notify(text, kind) { mobile.notice(text, kind || 'info'); }
function tag(text, kind) { return E('span', { 'class': 'ui-tag ' + (kind || '') }, text); }
function known(value) { return !mobile.missing(value); }
function normalize(number) { return String(number || '').replace(/[\s()-]/g, ''); }
function displayTime(value) { return known(value) ? String(value).replace('T', ' ') : ''; }
function messageEpoch(message) {
	var raw = String(message.timestamp || '').replace('T', ' ');
	var match = /^(\d{2,4})-(\d{2})-(\d{2}) (\d{2}):(\d{2})(?::(\d{2}))?/.exec(raw);
	if (match) {
		var year = Number(match[1]);
		if (year < 100) year += 2000;
		return new Date(year, Number(match[2]) - 1, Number(match[3]), Number(match[4]), Number(match[5]), Number(match[6] || 0)).getTime();
	}
	return Number(message.timestamp_epoch || 0) * 1000;
}
function sortMessages(messages) {
	return messages.sort(function(a, b) { return messageEpoch(b) - messageEpoch(a); });
}
function deduplicateMessages(messages) {
	var known = {}, result = [];
	(messages || []).forEach(function(message) {
		var key = messageKey(message), previous = known[key];
		if (previous && (previous.source === 'sim' || message.source === 'sim')) {
			(message.record_ids || []).forEach(function(id) {
				if (previous.record_ids.indexOf(id) < 0) previous.record_ids.push(id);
			});
			if (message.read !== true) previous.read = false;
			return;
		}
		known[key] = message;
		result.push(message);
	});
	return result;
}
function messageKey(message) {
	return [ message.direction || '', normalize(message.number), message.timestamp || '', message.text || '' ].join('|');
}

function applyDeliveryReceipts(messages, receipts) {
	var available = {};
	(receipts || []).forEach(function(receipt) {
		var key = [ receipt.slot || '', String(receipt.message_id) ].join(':');
		if (!available[key] || Number(receipt.timestamp_epoch || 0) > Number(available[key].timestamp_epoch || 0))
			available[key] = receipt;
	});
	return (messages || []).map(function(original) {
		var message = Object.assign({}, original);
		if (message.direction !== 'out') return message;
		var ids = Array.isArray(message.message_ids) && message.message_ids.length ? message.message_ids : [ message.message_id ];
		var states = [];
		ids.forEach(function(id) {
			var key = [ message.slot || '', String(id) ].join(':'), receipt = available[key];
			if (!receipt || Number(receipt.timestamp_epoch || 0) < Number(message.timestamp_epoch || 0)) return;
			states.push(receipt.delivery || 'unknown');
			delete available[key];
		});
		if (states.indexOf('failed') >= 0) message.delivery = 'failed';
		else if (states.length === ids.length && states.every(function(state) { return state === 'delivered'; })) message.delivery = 'delivered';
		else if (states.indexOf('unknown') >= 0) message.delivery = 'unknown';
		else message.delivery = 'pending';
		message.delivered_segments = states.filter(function(state) { return state === 'delivered'; }).length;
		return message;
	});
}

function deliveryLabel(message) {
	if (message.direction !== 'out') return '';
	switch (message.delivery) {
	case 'delivered': return _('Delivered');
	case 'failed': return _('Delivery failed');
	case 'unknown': return _('Receipt received');
	default: return _('Sent · awaiting receipt');
	}
}

function mergeMultipart(messages) {
	var groups = {}, output = [];
	(messages || []).forEach(function(message) {
		var total = Number(message.concat_total || 0), part = Number(message.concat_part || 0);
		if (total <= 1 || part < 1 || part > total || message.concat_ref === undefined) {
			var single = Object.assign({}, message);
			single.record_ids = message.record_id ? [ message.record_id ] : [];
			output.push(single);
			return;
		}
		var key = [ message.direction || '', normalize(message.number), message.concat_ref, total ].join('|');
		if (!groups[key]) {
			groups[key] = {
				message: Object.assign({}, message), parts: new Array(total), count: 0
			};
			groups[key].message.record_ids = [];
			output.push(groups[key].message);
		}
		if (message.record_id && groups[key].message.record_ids.indexOf(message.record_id) < 0)
			groups[key].message.record_ids.push(message.record_id);
		if (message.read !== true) groups[key].message.read = false;
		if (groups[key].parts[part - 1] === undefined) groups[key].count++;
		groups[key].parts[part - 1] = message.text || '';
		groups[key].message.timestamp = groups[key].message.timestamp || message.timestamp;
	});
	Object.keys(groups).forEach(function(key) {
		var group = groups[key];
		group.message.text = group.parts.map(function(part, index) {
			return part === undefined ? _('[Missing part %d]').format(index + 1) : part;
		}).join('');
		group.message.multipart_complete = group.count === group.parts.length;
	});
	return deduplicateMessages(sortMessages(output));
}

function conversations(messages) {
	var map = {}, order = [];
	(messages || []).forEach(function(message) {
		var number = normalize(message.number) || _('Unknown sender');
		if (!map[number]) { map[number] = []; order.push(number); }
		map[number].push(message);
	});
	return { map: map, order: order };
}

return view.extend({
	load: function() {
		/* The telephony capability cache is intentionally read-only and is not
		 * refreshed by the high-frequency status poll.  Populate it before the
		 * first render so an empty boot-time cache cannot incorrectly disable
		 * the real VOICE/WMS controls. */
		return mobile.action('refresh-telecom').catch(function() { return null; }).then(function() {
				return mobile.action('sms-sync').catch(function() { return null; });
		}).then(function() {
				return Promise.all([
					mobile.status(),
					mobile.action('messages').catch(function() { return { messages: [], receive_active: false }; }),
					mobile.action('call-history').catch(function() { return { calls: [] }; })
				]);
		});
	},

	render: function(result) {
			var status = result[0], mailbox = result[1], history = result[2].calls || [];
			var messages = mergeMultipart(applyDeliveryReceipts(mailbox.messages || [], mailbox.receipts || []));
			var activeTab = 'sms', selectedPeer = null, viewingPeer = null, draftRecipient = '', draftText = '', dialNumber = '';
		var content = E('div', { 'class': 'xm14-communication-content' });
		var tabs = E('div', { 'class': 'xm14-segmented' });
		var knownMessages = {}, previousIncomingCall = '';
		var normalTitle = document.title;
		var notificationEnabled = window.localStorage.getItem('xm14pro.notifications') === '1';
		var notificationButton, screenButton;

		messages.forEach(function(message) { knownMessages[messageKey(message)] = true; });
		if (Number(status.active_call_count || 0) > 0 && [ 'incoming', 'waiting' ].indexOf(String(status.active_call_state || '').toLowerCase()) >= 0)
			previousIncomingCall = [ status.active_call_id, status.active_call_number ].join('|');

		function notificationCapability() {
			if (!('Notification' in window)) return 'unsupported';
			if (!window.isSecureContext) return 'insecure';
			return window.Notification.permission;
		}

		function notificationLabel() {
			var capability = notificationCapability();
			if (!notificationEnabled) return _('Enable notifications');
			return capability === 'granted' ? _('Notifications on') : _('In-page notifications on');
		}

		function updateNotificationButton() {
			if (!notificationButton) return;
			var label = notificationButton.querySelector('span:last-child');
			if (label) label.textContent = notificationLabel();
			notificationButton.classList.toggle('is-on', notificationEnabled);
			notificationButton.disabled = false;
		}

		function toggleNotifications() {
			var capability = notificationCapability();
			if (capability === 'default') {
				window.Notification.requestPermission().then(function(permission) {
					notificationEnabled = true;
					window.localStorage.setItem('xm14pro.notifications', '1');
					mobile.startTelephonyNotifications();
					if (permission !== 'granted')
						notify(_('In-page call and message notifications are enabled. HTTPS is required for system notifications.'));
					updateNotificationButton();
				});
				return;
			}
			notificationEnabled = !notificationEnabled;
			window.localStorage.setItem('xm14pro.notifications', notificationEnabled ? '1' : '0');
			if (notificationEnabled) mobile.startTelephonyNotifications();
			if (notificationEnabled && capability !== 'granted')
				notify(_('In-page call and message notifications are enabled. HTTPS is required for system notifications.'));
			updateNotificationButton();
		}

		function browserNotification(title, body, tab, tagName, persistent) {
			if (!notificationEnabled || notificationCapability() !== 'granted') return;
			var item = new window.Notification(title, { body: body, tag: tagName, requireInteraction: !!persistent, lang: 'zh-CN' });
			item.onclick = function() { window.focus(); switchTab(tab); item.close(); };
		}

		function announceEvent(title, body, tab, tagName, persistent) {
			notify(title + (body ? '\n' + body : ''), 'info');
			if (document.hidden) document.title = title + ' · nwrt';
			if (navigator.vibrate) navigator.vibrate(persistent ? [ 220, 120, 220 ] : 160);
			browserNotification(title, body, tab, tagName, persistent);
		}

		function detectNewMessages(nextMessages) {
			(nextMessages || []).forEach(function(message) {
				var key = messageKey(message);
					if (!knownMessages[key] && message.direction === 'in' && message.read !== true &&
					    !(!document.hidden && activeTab === 'sms' && normalize(selectedPeer) === normalize(message.number)))
					announceEvent(_('New message'), '%s\n%s'.format(normalize(message.number) || _('Unknown sender'), message.text || ''), 'sms', 'sms-' + key, false);
				knownMessages[key] = true;
			});
		}

		function detectIncomingCall(nextStatus) {
			var state = String(nextStatus.active_call_state || '').toLowerCase();
			var incoming = Number(nextStatus.active_call_count || 0) > 0 && [ 'incoming', 'waiting' ].indexOf(state) >= 0;
			var signature = incoming ? [ nextStatus.active_call_id, nextStatus.active_call_number ].join('|') : '';
			if (signature && signature !== previousIncomingCall)
				announceEvent(_('Incoming call'), nextStatus.active_call_number || _('Private number'), 'phone', 'call-' + signature, true);
			previousIncomingCall = signature;
		}

		function act(action, args, success) {
			return mobile.action(action, args || []).then(function(response) {
				if (success) notify(success, 'success');
				return response;
			}).catch(function(error) { mobile.notifyError(error); throw error; });
		}

		function fetchStatus() {
			return mobile.status().then(function(next) { status = next; return next; });
		}

		function fetchMessages() {
			return mobile.action('messages').then(function(next) {
				mailbox = next; messages = mergeMultipart(applyDeliveryReceipts(next.messages || [], next.receipts || [])); return next;
			});
		}

			function fetchHistory() {
				return mobile.action('call-history').then(function(next) {
					history = next.calls || [];
					return next;
				});
			}

			function markThreadViewed(number) {
				var target = normalize(number), changed = false;
				if (!/^\+?[0-9*#]+$/.test(target)) return;
				messages.forEach(function(message) {
					if (message.direction === 'in' && normalize(message.number) === target && message.read !== true) {
						message.read = true;
						changed = true;
					}
				});
				if (changed)
					mobile.action('sms-read-thread', [ target, '1' ]).catch(function() { fetchMessages(); });
			}

		function switchTab(next) {
			activeTab = next;
				if (next !== 'sms') viewingPeer = null;
			Array.prototype.forEach.call(tabs.children, function(button) { button.classList.toggle('active', button.getAttribute('data-tab') === next); });
			renderContent();
		}

		function tabButton(id, label, iconName) {
			return E('button', { 'type': 'button', 'class': id === activeTab ? 'active' : '', 'data-tab': id, 'click': function() { switchTab(id); } }, [ mobile.icon(iconName, 'plain', 'ui-button-icon'), E('span', {}, label) ]);
		}

		tabs.appendChild(tabButton('sms', _('Messages'), 'message'));
		tabs.appendChild(tabButton('phone', _('Phone'), 'phone'));

		function smsParameters() {
			var routeDetail = [ status.sms_route_count ? _('%s routes').format(status.sms_route_count) : '', status.sms_transfer_route === true ? _('Direct delivery') : '' ].filter(Boolean).join(' · ');
			var items = [
				[ _('Receiving'), mailbox.receive_active ? _('Listening') : _('Stopped'), mailbox.receive_active ? 'is-good' : 'is-warn' ],
					[ _('SMS SIM'), mobile.slot(status.sms_slot), '' ],
					[ _('Unread'), String(messages.filter(function(message) { return message.direction === 'in' && message.read !== true; }).length), '' ],
				known(status.smsc_address) ? [ _('SMSC'), status.smsc_address, '' ] : null,
				[ _('Message route'), routeDetail || (status.sms_route_query ? _('Available') : _('Unavailable')), status.sms_route_query ? 'is-good' : 'is-warn' ],
				known(status.sms_stored_total) ? [ _('Stored / unread'), '%s / %s'.format(status.sms_stored_total, mobile.text(status.sms_unread_total, '0')), '' ] : null,
				!known(status.sms_stored_total) && known(status.sms_unread_total) ? [ _('Unread') + ' (SIM)', String(status.sms_unread_total), '' ] : null,
				known(status.ims_sms_status) || known(status.ims_sms_technology) ? [ _('IMS messages'), mobile.servicePair(status.ims_sms_status, status.ims_sms_technology), '' ] : null
			].filter(Boolean);
			return E('section', { 'class': 'xm14-sms-statusbar' }, items.map(function(item) {
				return E('div', { 'class': 'xm14-sms-status ' + item[2] }, [ E('small', {}, item[0]), E('strong', {}, item[1]) ]);
			}));
		}

		function renderSms() {
			var grouped = conversations(messages);
			if (!document.hidden && selectedPeer && viewingPeer !== selectedPeer) {
				viewingPeer = selectedPeer;
				markThreadViewed(selectedPeer);
			}
			var threadList = E('div', { 'class': 'xm14-thread-list' });
			var bubbles = E('div', { 'class': 'xm14-bubbles' });
			var recipient = E('input', {
				'type': 'tel', 'placeholder': _('Recipient number'), 'autocomplete': 'tel',
				'value': selectedPeer || draftRecipient,
				'input': function() { draftRecipient = this.value; }
			});
			var body = E('textarea', {
				'placeholder': _('Write a message'), 'value': draftText,
				'input': function() { draftText = this.value; updateCounter(); }
			});
			var counter = E('small', { 'class': 'xm14-sms-counter' });
			function updateCounter() {
				var units = body.value.length, segments = units ? (units <= 70 ? 1 : Math.ceil(units / 67)) : 0;
				counter.textContent = _('%d characters · %d SMS').format(Array.from(body.value).length, segments);
			}
			updateCounter();

			function deleteMessage(message) {
				var ids = message.record_ids || (message.record_id ? [ message.record_id ] : []);
				if (!ids.length || !window.confirm(_('Delete this message?'))) return;
				act('sms-delete', [ ids.join(',') ], _('Message deleted.'))
					.then(fetchMessages).then(renderContent);
			}

			function setMessageRead(message, read) {
				var ids = message.record_ids || (message.record_id ? [ message.record_id ] : []);
				if (!ids.length) return;
				act('sms-read', [ ids.join(','), read ? '1' : '0' ], read ? _('Marked as read.') : _('Marked as unread.')).then(function() {
					messages.forEach(function(item) {
						if ((item.record_ids || []).some(function(id) { return ids.indexOf(id) >= 0; })) item.read = read;
					});
					renderContent();
				});
			}

			function forwardMessage(message) {
				selectedPeer = null;
				viewingPeer = null;
				draftRecipient = '';
				draftText = message.text || '';
				renderContent();
				var target = content.querySelector('input[type="tel"]');
				if (target) target.focus();
			}

			function deleteThread(number) {
				var target = normalize(number || selectedPeer);
				if (!/^\+?[0-9*#]+$/.test(target)) return;
				if (!target || !window.confirm(_('Delete all messages with %s?').format(target))) return;
				act('sms-delete-thread', [ target ], _('Conversation deleted.')).then(function() {
					if (normalize(selectedPeer) === target) selectedPeer = null;
					return fetchMessages();
				}).then(renderContent);
			}

			function deleteAllMessages() {
				if (!messages.length || !window.confirm(_('Delete all messages? This cannot be undone.'))) return;
				act('sms-delete-all', [], _('All messages deleted.')).then(function() {
					selectedPeer = null;
					return fetchMessages();
				}).then(renderContent);
			}

			threadList.appendChild(E('div', { 'class': 'xm14-thread-toolbar' }, [
				E('strong', {}, _('Conversations')),
				E('span', { 'class': 'xm14-thread-actions' }, [
					E('button', { 'type': 'button', 'class': 'danger', 'title': _('Delete all messages'), 'disabled': !messages.length ? '' : null, 'click': deleteAllMessages }, '×'),
					E('button', { 'type': 'button', 'title': _('New conversation'), 'click': function() { selectedPeer = null; draftRecipient = ''; draftText = ''; renderContent(); } }, '+')
				])
			]));
			if (!grouped.order.length) threadList.appendChild(E('div', { 'class': 'xm14-chat-empty compact' }, _('No messages yet.')));
			grouped.order.forEach(function(number) {
				var list = grouped.map[number], latest = list[0] || {};
				var unread = list.filter(function(message) { return message.direction === 'in' && message.read !== true; }).length;
				threadList.appendChild(E('div', { 'class': 'xm14-thread-entry' }, [
					E('button', {
						'type': 'button', 'class': 'xm14-thread ' + (selectedPeer === number ? 'active' : ''),
						'click': function() { selectedPeer = number; draftRecipient = number; renderContent(); }
					}, [
						E('span', { 'class': 'xm14-avatar' }, number.slice(-2)),
						E('span', { 'class': 'xm14-thread-copy' }, [ E('strong', {}, number), E('small', {}, latest.text || _('Message')) ]),
						E('span', { 'class': 'xm14-thread-meta' }, [ E('time', {}, displayTime(latest.timestamp).slice(-5)), unread ? E('b', {}, String(unread)) : '' ])
					]),
					E('button', { 'type': 'button', 'class': 'xm14-thread-delete', 'title': _('Delete conversation'), 'disabled': !/^\+?[0-9*#]+$/.test(normalize(number)) ? '' : null, 'click': function() { deleteThread(number); } }, '×')
				]));
			});

			var selectedMessages = selectedPeer && grouped.map[selectedPeer] ? grouped.map[selectedPeer].slice().reverse() : [];
			if (!selectedMessages.length) bubbles.appendChild(E('div', { 'class': 'xm14-chat-empty' }, _('Start a new conversation.')));
			selectedMessages.forEach(function(message) {
				bubbles.appendChild(E('div', { 'class': 'xm14-bubble-row ' + (message.direction === 'out' ? 'out' : 'in') + (message.read !== true ? ' unread' : '') }, [
					E('div', { 'class': 'xm14-bubble' }, [
						E('p', {}, message.text || ''),
						E('time', {}, [ displayTime(message.timestamp), deliveryLabel(message) ? ' · ' + deliveryLabel(message) : '' ]),
						E('div', { 'class': 'xm14-message-actions' }, [
							E('button', { 'type': 'button', 'title': _('Forward'), 'click': function() { forwardMessage(message); } }, _('Forward')),
							message.direction === 'in' ? E('button', { 'type': 'button', 'title': message.read === true ? _('Mark as unread') : _('Mark as read'), 'disabled': !(message.record_ids || []).length ? '' : null, 'click': function() { setMessageRead(message, message.read !== true); } }, message.read === true ? _('Unread') : _('Read')) : '',
							E('button', { 'type': 'button', 'class': 'xm14-message-delete', 'title': _('Delete message'), 'disabled': !(message.record_ids || []).length ? '' : null, 'click': function() { deleteMessage(message); } }, '×')
						])
					])
				]));
			});

			function send() {
				var target = normalize(selectedPeer || draftRecipient || recipient.value), text = body.value.trim();
				if (!target || !text) { notify(_('Enter a recipient and message.'), 'error'); return; }
				draftText = text;
				act('sms-send', [ target, text ], _('Message sent.')).then(function() {
					selectedPeer = target; draftRecipient = target; draftText = '';
					return fetchMessages();
				}).then(renderContent);
			}

			return [ smsParameters(), E('section', { 'class': 'xm14-chat-shell' }, [
				E('aside', { 'class': 'xm14-chat-sidebar' }, threadList),
				E('div', { 'class': 'xm14-chat-main' }, [
					E('div', { 'class': 'xm14-chat-header' }, [ E('div', {}, [ E('strong', {}, selectedPeer || _('New conversation')), E('small', {}, mobile.slot(status.sms_slot)) ]), mailbox.receive_active ? tag(_('Online'), 'is-good') : tag(_('Offline'), 'is-warn') ]),
					bubbles,
					E('div', { 'class': 'xm14-chat-composer' }, [ selectedPeer ? '' : recipient, body, counter, mobile.button(_('Send'), 'cbi-button-positive', send, !status.sms_send_control, 'arrowUp') ])
				])
			]) ];
		}

		function renderPhone() {
			var activeCalls = Number(status.active_call_count || 0), callState = String(status.active_call_state || '').toLowerCase();
			var canAnswer = activeCalls > 0 && (callState === 'incoming' || callState === 'waiting');
			var canDtmf = activeCalls > 0 && [ 'conversation', 'hold' ].indexOf(callState) >= 0;
			var display = E('input', {
				'type': 'tel', 'class': 'xm14-dial-display', 'placeholder': _('Enter phone number'), 'autocomplete': 'tel',
				'value': activeCalls ? (status.active_call_number || '') : dialNumber, 'readonly': activeCalls ? '' : null,
				'input': function() { dialNumber = this.value; }
			});
			var keys = [ [ '1', '' ], [ '2', 'ABC' ], [ '3', 'DEF' ], [ '4', 'GHI' ], [ '5', 'JKL' ], [ '6', 'MNO' ], [ '7', 'PQRS' ], [ '8', 'TUV' ], [ '9', 'WXYZ' ], [ '*', '' ], [ '0', '+' ], [ '#', '' ] ];
			var keypad = E('div', { 'class': 'xm14-keypad' }, keys.map(function(key) {
				return E('button', { 'type': 'button', 'disabled': activeCalls && !canDtmf ? '' : null, 'click': function() {
					if (activeCalls) {
						act('call-dtmf', [ status.active_call_id, key[0] ]).catch(function() {});
						return;
					}
					dialNumber += key[0]; display.value = dialNumber; display.focus();
				} }, [ E('strong', {}, key[0]), E('small', {}, key[1] || '\u00a0') ]);
			}));

			function callAction(action, args, message) {
				return act(action, args, message).then(function() {
					return new Promise(function(resolve) { window.setTimeout(resolve, 800); });
				}).then(fetchStatus).then(renderContent);
			}

			function audioAction(action, value, message) {
				return act(action, [ String(value) ], message).then(fetchStatus).then(renderContent);
			}

			var controls = activeCalls ? E('div', { 'class': 'xm14-call-controls' }, [
				canAnswer ? E('button', { 'type': 'button', 'class': 'xm14-call-round answer', 'title': _('Answer'), 'click': function() { callAction('call-answer', [ status.active_call_id ]); } }, mobile.icon('phone', 'plain', 'ui-button-icon')) : '',
				E('button', { 'type': 'button', 'class': 'xm14-call-round hangup', 'title': _('Hang up'), 'click': function() { callAction('call-end', [ status.active_call_id ]); } }, mobile.icon('phone', 'plain', 'ui-button-icon'))
			]) : E('div', { 'class': 'xm14-call-controls' }, [
				E('button', { 'type': 'button', 'class': 'xm14-call-round call', 'title': _('Call'), 'disabled': !status.voice_call_control ? '' : null, 'click': function() {
					var target = normalize(display.value);
					if (!target) { notify(_('Enter a phone number.'), 'error'); return; }
					dialNumber = target; callAction('call-dial', [ target ]);
				} }, mobile.icon('phone', 'plain', 'ui-button-icon')),
				E('button', { 'type': 'button', 'class': 'xm14-dial-delete', 'title': _('Delete'), 'click': function() { dialNumber = dialNumber.slice(0, -1); display.value = dialNumber; } }, '⌫')
			]);

			var callInfo = activeCalls ? E('section', { 'class': 'ui-card xm14-active-call' }, [
				E('span', { 'class': 'xm14-call-pulse' }), E('small', {}, mobile.stateText(status.active_call_state)),
				E('strong', {}, status.active_call_number || _('Private number')),
				E('p', {}, [ status.active_call_direction, status.active_call_type, status.active_call_mode ].filter(known).join(' · ')), controls
			]) : E('section', { 'class': 'ui-card xm14-active-call idle' }, [ E('small', {}, _('Phone')), E('strong', {}, _('Ready to dial')), E('p', {}, mobile.slot(status.voice_slot)) ]);

			var route = status.voice_audio_route || 'earpiece', audioActive = status.voice_audio_active === true;
			var callVolume = Number(status.voice_call_volume == null ? 50 : status.voice_call_volume);
			var volumeOutput = E('strong', {}, callVolume + '%');
			var volume = E('input', {
				'type': 'range', 'min': 0, 'max': 100, 'step': 1, 'value': callVolume,
				'disabled': !status.voice_audio_bridge ? '' : null,
				'input': function() { volumeOutput.textContent = this.value + '%'; },
				'change': function() { audioAction('call-audio-volume', this.value, _('Call volume updated.')); }
			});
			var audioPanel = E('section', { 'class': 'ui-card xm14-call-audio' }, [
				E('div', { 'class': 'xm14-call-audio-title' }, [ E('strong', {}, _('Call audio')), tag(audioActive ? _('Active') : _('Standby'), audioActive ? 'is-good' : 'is-warn') ]),
				E('div', { 'class': 'xm14-call-audio-buttons' }, [
					E('button', { 'type': 'button', 'class': route === 'earpiece' ? 'active' : '', 'disabled': !audioActive ? '' : null, 'click': function() { audioAction('call-audio-route', 'earpiece', _('Switched to earpiece.')); } }, [ mobile.icon('phone', 'plain', 'ui-button-icon'), E('span', {}, _('Earpiece')) ]),
					E('button', { 'type': 'button', 'class': route === 'speaker' ? 'active' : '', 'disabled': !audioActive ? '' : null, 'click': function() { audioAction('call-audio-route', 'speaker', _('Speaker enabled.')); } }, [ mobile.icon('volume', 'plain', 'ui-button-icon'), E('span', {}, _('Speaker')) ]),
					E('button', { 'type': 'button', 'class': status.voice_mic_muted ? 'active danger' : '', 'disabled': !audioActive ? '' : null, 'click': function() { audioAction('call-audio-mute', status.voice_mic_muted ? 0 : 1, status.voice_mic_muted ? _('Microphone enabled.') : _('Microphone muted.')); } }, [ mobile.icon('audio', 'plain', 'ui-button-icon'), E('span', {}, status.voice_mic_muted ? _('Unmute') : _('Mute')) ])
				]),
				E('label', { 'class': 'xm14-call-volume' }, [ E('span', {}, [ E('b', {}, _('Call volume')), volumeOutput ]), volume ])
			]);

			function historyDate(epoch) {
				var value = Number(epoch || 0);
				if (!value) return _('Not reported');
				return new Date(value * 1000).toLocaleString('zh-CN', {
					year: 'numeric', month: '2-digit', day: '2-digit',
					hour: '2-digit', minute: '2-digit', hour12: false
				});
			}

			function historyDuration(seconds, answered) {
				var value = Math.max(0, Number(seconds || 0));
				if (!Number(answered || 0)) return _('Not answered');
				var minutes = Math.floor(value / 60), remainder = Math.floor(value % 60);
				return '%d:%s'.format(minutes, String(remainder).padStart(2, '0'));
			}

			function historyLabel(value) {
				return ({
					incoming: _('Incoming'), outgoing: _('Outgoing'),
					completed: _('Completed'), missed: _('Missed'), rejected: _('Rejected'),
					cancelled: _('Cancelled'), failed: _('Failed')
				})[value] || mobile.stateText(value);
			}

			function historySlot(value) {
				var match = /^slot([12])$/.exec(String(value || ''));
				return mobile.slot(match ? match[1] : value);
			}

			var historyList = E('div', { 'class': 'xm14-call-history-list' });
			if (!history.length)
				historyList.appendChild(E('div', { 'class': 'xm14-call-history-empty' }, _('No call records yet.')));
			history.slice(0, 100).forEach(function(call) {
				var direction = call.direction === 'outgoing' ? 'outgoing' : 'incoming';
				var statusClass = call.status === 'missed' || call.status === 'failed' ? 'is-danger' : '';
				historyList.appendChild(E('div', { 'class': 'xm14-call-history-item ' + statusClass }, [
					E('span', { 'class': 'xm14-call-history-direction ' + direction }, mobile.icon('phone', 'plain', 'ui-button-icon')),
					E('span', { 'class': 'xm14-call-history-main' }, [
						E('strong', {}, call.number || _('Private number')),
						E('small', {}, '%s · %s · %s'.format(historyLabel(direction), historySlot(call.slot), historyDate(call.started_epoch)))
					]),
					E('span', { 'class': 'xm14-call-history-result' }, [
						E('strong', {}, historyLabel(call.status)),
						E('small', {}, historyDuration(call.duration_seconds, call.answered_epoch))
					])
				]));
			});
			var historyPanel = E('section', { 'class': 'ui-group xm14-call-history' }, [
				E('div', { 'class': 'ui-group-head xm14-call-history-title' }, [
					E('h2', {}, _('Call history')),
					E('small', {}, _('%d records').format(history.length))
				]),
				E('div', { 'class': 'ui-card xm14-call-history-scroll' }, historyList)
			]);

			var voiceRows = [
				known(status.ims_registration) ? mobile.row(_('IMS registration'), mobile.stateText(status.ims_registration)) : null,
				known(status.ims_voice_status) || known(status.ims_voice_technology) ? mobile.row(_('IMS voice'), mobile.servicePair(status.ims_voice_status, status.ims_voice_technology)) : null,
				known(status.voice_domain) ? mobile.row(_('Voice domain'), status.voice_domain) : null,
				known(status.voice_privacy) ? mobile.row(_('Voice privacy'), status.voice_privacy) : null,
				known(status.tty_mode) ? mobile.row(_('TTY mode'), status.tty_mode) : null,
				mobile.row(_('Call-state listener'), status.voice_listener_running ? tag(_('Running'), 'is-good') : tag(_('Stopped'), 'is-warn')),
				mobile.row(_('Voice audio endpoint'), status.voice_audio_bridge ? tag(_('Ready'), 'is-good') : tag(_('Unavailable'), 'is-warn')),
				mobile.row(_('ALSA sound cards'), String(status.audio_card_count || 0))
			].filter(Boolean);
			var voicePanel = mobile.card(_('Voice service'), voiceRows, 'xm14-voice-service');

			return [ E('div', { 'class': 'xm14-phone-shell' }, [
				E('section', { 'class': 'ui-card xm14-dialer' }, [ E('div', { 'class': 'xm14-dial-input' }, display), keypad, controls ]),
				E('div', { 'class': 'xm14-phone-side' }, [ callInfo, audioPanel ])
			]), E('div', { 'class': 'xm14-phone-service-grid' }, [ historyPanel, voicePanel ]) ];
		}

		function renderContent() {
			content.replaceChildren.apply(content, activeTab === 'sms' ? renderSms() : renderPhone());
		}

		function refreshAll() {
			return mobile.action('refresh-telecom').catch(function() { return null; })
				.then(function() { return mobile.action('sms-sync').catch(function(error) { mobile.notifyError(error); return null; }); })
				.then(function() { return Promise.all([ fetchMessages(), fetchHistory() ]); })
				.then(fetchStatus).then(renderContent).catch(mobile.notifyError);
		}

		function updateScreenButton() {
			if (!screenButton) return;
			var enabled = status.screen_telephony_pages === true;
			var label = screenButton.querySelector('span:last-child');
			if (label) label.textContent = enabled ? _('屏幕短信电话：已启用') : _('屏幕短信电话：已隐藏');
			screenButton.classList.toggle('is-on', enabled);
			screenButton.disabled = false;
		}

		function toggleScreenTelephony() {
			var next = status.screen_telephony_pages === true ? '0' : '1';
			screenButton.disabled = true;
			act('screen-telephony', [ next ], next === '1' ? _('手机屏幕已显示短信和电话。') : _('手机屏幕已隐藏短信和电话。'))
				.then(fetchStatus).then(function() { updateScreenButton(); renderContent(); })
				.catch(function(e) { screenButton.disabled = false; mobile.notifyError(e); });
		}

		notificationButton = mobile.button(notificationLabel(), '', toggleNotifications, false, 'message');
		screenButton = mobile.button('', '', toggleScreenTelephony, false, 'phone');
		updateScreenButton();
		var page = mobile.page([
			E('div', { 'class': 'xm14-comm-service-strip' }, [
				mobile.datum(_('IMS registration'), mobile.stateText(status.ims_registration), status.ims_technology, String(status.ims_registration || '').toLowerCase() === 'registered' ? 'is-good' : 'is-warn'),
				mobile.datum(_('Voice SIM'), mobile.slot(status.voice_slot), status.voice_domain),
				mobile.datum(_('Messages SIM'), mobile.slot(status.sms_slot), status.smsc_address),
				mobile.datum(_('Active calls'), String(status.active_call_count || 0), mobile.stateText(status.active_call_state), Number(status.active_call_count || 0) > 0 ? 'is-good' : '')
			]),
			E('div', { 'class': 'xm14-comm-toolbar' }, [ tabs, E('div', { 'class': 'xm14-comm-actions' }, [ screenButton, notificationButton, mobile.button(_('Refresh'), '', refreshAll, false, 'refresh') ]) ]),
			content
		], 'xm14-communication-page');
		updateNotificationButton();
		document.addEventListener('visibilitychange', function() {
			if (document.hidden) return;
			document.title = normalTitle;
			if (activeTab === 'sms' && selectedPeer) {
				markThreadViewed(selectedPeer);
				renderContent();
			}
		});
		renderContent();

		var previousCallSignature = [ status.active_call_count, status.active_call_id, status.active_call_state, status.active_call_number ].join('|');
			var previousActiveCalls = Number(status.active_call_count || 0);
		var previousMessages = JSON.stringify(messages);
		poll.add(function() {
			return Promise.all([ mobile.status(), mobile.action('messages') ]).then(function(next) {
				detectIncomingCall(next[0]);
				detectNewMessages(next[1].messages || []);
				status = next[0]; mailbox = next[1]; messages = mergeMultipart(applyDeliveryReceipts(mailbox.messages || [], mailbox.receipts || []));
				if (!document.hidden && activeTab === 'sms' && selectedPeer) markThreadViewed(selectedPeer);
				updateScreenButton();
				var callSignature = [ status.active_call_count, status.active_call_id, status.active_call_state, status.active_call_number ].join('|');
				var messageSignature = JSON.stringify(messages);
				var callEnded = previousActiveCalls > 0 && Number(status.active_call_count || 0) === 0;
				previousActiveCalls = Number(status.active_call_count || 0);
				if (callEnded) {
					return fetchHistory().then(function() {
						if (activeTab === 'phone') renderContent();
						previousCallSignature = callSignature;
						previousMessages = messageSignature;
					});
				}
				if ((activeTab === 'phone' && callSignature !== previousCallSignature) || (activeTab === 'sms' && messageSignature !== previousMessages)) renderContent();
				previousCallSignature = callSignature; previousMessages = messageSignature;
			}).catch(function() {});
		}, 3);
		return page;
	},
	handleSaveApply: null, handleSave: null, handleReset: null
});
