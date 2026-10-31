'use strict';
'require view';
'require fs';
'require ui';

var COMMAND = '/usr/libexec/xm14pro-android-switch';

function result(reply) {
	var data;
	try { data = JSON.parse((reply && reply.stdout) || '{}'); }
	catch (e) { throw new Error(_('The switch command returned invalid data')); }
	if (!data.ok) throw new Error(data.error || _('Unable to switch to Android'));
	return data;
}

return view.extend({
	render: function() {
		var button = E('button', {
			'class': 'btn cbi-button cbi-button-action important',
			'click': function() {
				ui.showModal(_('切换到 Android'), [
					E('p', {}, _('确定重启进入 Android 系统？')),
					E('div', { 'class': 'right' }, [
						E('button', { 'class': 'btn', 'click': ui.hideModal }, _('取消')),
						' ',
						E('button', { 'class': 'btn cbi-button-action important', 'click': function(ev) {
							ev.currentTarget.disabled = true;
							ev.currentTarget.textContent = _('正在切换…');
							fs.exec(COMMAND, [ 'switch' ]).then(result).then(function() {
								ui.showModal(_('正在重启'), [ E('p', {}, _('设备正在进入 A 槽 Android。')) ]);
							}).catch(function(e) {
								ui.addNotification(null, E('p', {}, e.message), 'error');
								ui.hideModal();
							});
						} }, _('确认切换并重启'))
					])
				]);
			}
		}, _('进入 Android'));

		return E('div', { 'class': 'cbi-map' }, [
			E('div', { 'class': 'cbi-section' }, [
				button
			])
		]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
