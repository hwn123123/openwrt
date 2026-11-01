'use strict';
'require view';
'require fs';
'require ui';

var COMMAND = '/usr/libexec/xm14pro-android-switch';

function result(reply) {
	var data;
	try { data = JSON.parse((reply && reply.stdout) || '{}'); }
	catch (e) { throw new Error(_('设备操作返回了无效数据')); }
	if (!data.ok) throw new Error(data.error || _('设备操作失败'));
	return data;
}

function actionButton(label, title, question, command, success) {
	return E('button', {
		'class': 'btn cbi-button cbi-button-action important',
		'click': function() {
			ui.showModal(_(title), [
				E('p', {}, _(question)),
				E('div', { 'class': 'right' }, [
					E('button', { 'class': 'btn', 'click': ui.hideModal }, _('取消')),
					' ',
					E('button', { 'class': 'btn cbi-button-action important', 'click': function(ev) {
						ev.currentTarget.disabled = true;
						ev.currentTarget.textContent = _('正在切换…');
						fs.exec(COMMAND, [ command ]).then(result).then(function() {
							ui.showModal(_('正在重启'), [ E('p', {}, _(success)) ]);
						}).catch(function(e) {
							ui.addNotification(null, E('p', {}, e.message), 'error');
							ui.hideModal();
						});
					} }, _('确认切换并重启'))
				])
			]);
		}
	}, _(label));
}

return view.extend({
	render: function() {
		return E('div', { 'class': 'cbi-map' }, [
			E('div', { 'class': 'cbi-section' }, [
				actionButton('进入 Android', '切换到 Android',
					'确定重启进入 Android 系统？', 'switch', '设备正在进入 A 槽 Android。'),
				' ',
				actionButton('进入共享 REC', '切换到共享 REC',
					'确定重启进入 NWRT Recovery？', 'recovery', '设备正在进入共享 REC。')
			])
		]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
