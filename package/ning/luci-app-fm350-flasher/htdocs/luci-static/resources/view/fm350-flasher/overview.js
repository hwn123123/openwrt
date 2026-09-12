'use strict';
'require view';
'require fs';
'require poll';
'require ui';

document.head.appendChild(E('link', {
	'rel': 'stylesheet',
	'type': 'text/css',
	'href': L.resource('view/fm350-flasher/overview.css')
}));

const HELPER = '/usr/libexec/fm350-flasher';
const FIRMWARE_UPLOAD = '/tmp/fm350-firmware-upload';
const BACKUP_UPLOAD = '/tmp/fm350-backup-upload';

function execJSON(args) {
	return fs.exec_direct(HELPER, args, 'json');
}

function text(id, value) {
	const node = document.getElementById(id);
	if (node)
		node.textContent = value == null || value === '' ? '—' : String(value);
}

function notifyError(error) {
	ui.addNotification(null, E('p', {}, error && error.message ? error.message : String(error)), 'error');
}

function downloadBlob(blob, name) {
	const url = window.URL.createObjectURL(blob);
	const link = E('a', { 'style': 'display:none', 'href': url, 'download': name || 'FM350-full-backup.bin' });
	document.body.appendChild(link);
	link.click();
	document.body.removeChild(link);
	window.URL.revokeObjectURL(url);
}

function visibleJobLogs(lines) {
	return lines.filter(function(line) {
		/* A real failure must never be hidden. */
		if (/error|fail(?:ed)?|den(?:y|ied)|exception|失败|错误|拒绝/i.test(line))
			return true;

		/* Keep MTKClient's transport chatter in the backend job file. */
		return !/^(?:Preloader|DAXML|DaHandler|DAconfig|Port|Main)\s*-|^(?:Dumped offset|Wrote \/)|^fuse library not installed$/i.test(line);
	});
}

return view.extend({
	load: function() {
		return L.resolveDefault(execJSON([ 'status' ]), {});
	},

	runAndRefresh: function(args, successMessage) {
		return execJSON(args).then(L.bind(function() {
			if (successMessage)
				ui.addNotification(null, E('p', {}, successMessage), 'info');
			return this.refreshStatus();
		}, this)).catch(notifyError);
	},

	handleSetWorkspace: function() {
		const path = this.workspaceInput ? this.workspaceInput.value.trim() : '';
		if (!path) {
			notifyError(new Error(_('请输入工作区绝对路径。')));
			return;
		}
		this.workspacePathDirty = false;
		return this.runAndRefresh(
			[ 'set-workspace', path ],
			_('持久化工作区检测通过并已启用。')
		);
	},

	handleMountSelect: function(event) {
		const workspacePath = event.target.value;
		if (!workspacePath || !this.workspaceInput)
			return;
		this.workspaceInput.value = workspacePath;
		this.workspacePathDirty = true;
	},

	refreshStatus: function() {
		return L.resolveDefault(execJSON([ 'status' ]), null).then(L.bind(function(status) {
			if (status)
				this.updateStatus(status);
		}, this));
	},

	handleFirmwareUpload: function() {
		return ui.uploadFile(
			FIRMWARE_UPLOAD,
			null,
			_('上传后只识别是否为本机 FM350/MT6880 固件并读取刷写清单，不会立即刷写。')
			).then(L.bind(function(reply) {
				return execJSON([ 'import-firmware', reply.name || 'FM350-firmware' ]);
			}, this)).then(L.bind(function(result) {
				const accepted = result && result.state === 'accepted';
				if (!accepted)
					ui.addNotification(null, E('p', {},
						_('固件包已保存，但不是本机可用包或文件不完整。')), 'warning');
				return this.refreshStatus();
			}, this)).catch(notifyError);
	},

	handleFirmwareFlash: function(id, name) {
		const status = this.lastStatus || {};
		const job = status.job || {};
		const modem = status.modem || {};
		let blocked = '';

		if (job.state === 'waiting' || job.state === 'running')
			blocked = _('已有刷写任务正在运行。');
		else if (!(status.workspace && status.workspace.ready))
			blocked = _('工作区尚未启用。');
		else if (!status.backup)
			blocked = _('尚未准备同一模组的全量备份。');
		else if (!(status.tool && status.tool.ready))
			blocked = _('刷写执行层依赖不完整。');
		else if (!modem.compatible && !modem.automatic_power)
			blocked = _('未检测到 FM350，并且当前设备无法自动切换 WWAN 电源。');

		if (blocked) {
			ui.showModal(_('暂时不能刷写'), [
				E('p', {}, blocked),
				E('div', { 'class': 'fm-actions right' }, [
					E('button', { 'class': 'btn cbi-button-primary', 'click': ui.hideModal }, _('关闭'))
				])
			]);
			return Promise.resolve();
		}

		return execJSON([ 'select-firmware', id ]).then(L.bind(function() {
			return this.refreshStatus();
		}, this)).then(L.bind(function() {
			this.handleFlash(name);
		}, this)).catch(notifyError);
	},

	handleFirmwareDownload: function(id, name) {
		ui.showModal(_('准备下载'), [ E('p', { 'class': 'spinning' }, _('正在校验并准备固件原始压缩包…')) ]);
		return fs.exec_direct(HELPER, [ 'firmware-download', id ], 'blob').then(function(blob) {
			ui.hideModal();
			downloadBlob(blob, name);
		}).catch(function(error) {
			ui.hideModal();
			notifyError(error);
		});
	},

	handleFirmwareDelete: function(id, name) {
		ui.showModal(_('删除固件记录'), [
			E('p', {}, _('将只删除“%s”的原包、解压文件和识别记录，不会影响固件库中的其他版本或全量备份。').format(name || id)),
			E('div', { 'class': 'fm-actions right' }, [
				E('button', { 'class': 'btn', 'click': ui.hideModal }, _('取消')),
				E('button', {
					'class': 'btn cbi-button-negative important',
					'click': ui.createHandlerFn(this, function() {
						ui.hideModal();
						return this.runAndRefresh([ 'delete-firmware', id ], _('这个固件及其记录已删除。'));
					})
				}, _('确认删除'))
			])
		]);
	},

	handleBackupUpload: function() {
		return ui.uploadFile(
			BACKUP_UPLOAD,
			null,
			_('只接受精确的 0x1fefffff 字节 FM350 连续 NAND 全量备份。')
		).then(L.bind(function(reply) {
			return this.runAndRefresh(
				[ 'import-backup', reply.name || 'FM350-full-backup.bin' ],
				_('全量备份已导入并计算 SHA-256。')
			);
		}, this)).catch(notifyError);
	},

	handleStartBackup: function() {
		const automaticPower = !!(this.lastStatus && this.lastStatus.modem && this.lastStatus.modem.automatic_power);
		ui.showModal(_('制作 FM350 全量备份'), [
			E('p', {}, automaticPower ?
				_('设备会暂时断开蜂窝网络，并自动多次切换模组电源。备份约 511 MiB，完成后必须下载到电脑保存。') :
				_('设备会暂时断开蜂窝网络。当前硬件没有可用的 WWAN 电源控制，任务日志提示时需要手动断电再上电或重新插拔 FM350。')),
			E('div', { 'class': 'fm-actions right' }, [
				E('button', { 'class': 'btn', 'click': ui.hideModal }, _('取消')),
				E('button', {
					'class': 'btn cbi-button-positive important',
					'click': L.bind(function() {
						ui.hideModal();
						this.runAndRefresh([ 'start-backup' ], _('全量备份已在后台启动。'));
					}, this)
				}, _('开始备份'))
			])
		]);
	},

	handleBackupDownload: function() {
		ui.showModal(_('准备下载'), [ E('p', { 'class': 'spinning' }, _('正在校验全量备份并准备 511 MiB 文件…')) ]);
		return fs.exec_direct(HELPER, [ 'backup-download' ], 'blob').then(L.bind(function(blob) {
			ui.hideModal();
			downloadBlob(blob, this.lastStatus && this.lastStatus.backup && this.lastStatus.backup.name);
		}, this)).catch(function(error) {
			ui.hideModal();
			notifyError(error);
		});
	},

	handleBackupDelete: function() {
		ui.showModal(_('删除全量备份'), [
			E('p', {}, _('将删除工作区中的全量备份文件和记录，不会删除固件库中的任何固件。请确认已经把备份下载到电脑。')),
			E('div', { 'class': 'fm-actions right' }, [
				E('button', { 'class': 'btn', 'click': ui.hideModal }, _('取消')),
				E('button', {
					'class': 'btn cbi-button-negative important',
					'click': ui.createHandlerFn(this, function() {
						ui.hideModal();
						return this.runAndRefresh([ 'delete-backup' ], _('全量备份已删除。'));
					})
				}, _('确认删除'))
			])
		]);
	},

	handleFlash: function(firmwareName) {
		ui.showModal(_('确认刷写'), [
			E('div', { 'class': 'alert-message warning' }, [
				E('p', {}, [ _('将刷写：'), E('strong', {}, firmwareName || _('未命名固件')) ]),
				E('p', {}, _('刷写期间请保持稳定供电，不要重启设备或断开模组。'))
			]),
			E('div', { 'class': 'fm-actions right' }, [
				E('button', {
					'class': 'btn',
					'click': ui.hideModal
				}, _('取消')),
				E('button', {
					'class': 'btn cbi-button-negative important',
					'click': L.bind(function() {
						ui.hideModal();
						return this.runAndRefresh([ 'start-flash', 'FM350-GL', 'YES' ], _('刷写任务已启动，请勿重启或断电。'));
					}, this)
				}, _('开始刷写'))
			])
		]);
	},

	handleRestartModem: function() {
		ui.showModal(_('重启 FM350 模组'), [
			E('p', {}, _('将暂时断开蜂窝网络，关闭模组电源后重新上电；检测到模组 USB 再次出现即完成。')),
			E('div', { 'class': 'fm-actions right' }, [
				E('button', { 'class': 'btn', 'click': ui.hideModal }, _('取消')),
				E('button', {
					'class': 'btn cbi-button-action important',
					'click': L.bind(function() {
						ui.hideModal();
						return this.runAndRefresh([ 'restart-modem' ], _('模组重启任务已启动。'));
					}, this)
				}, _('确认重启'))
			])
		]);
	},

	handleCleanup: function() {
		ui.showModal(_('清理工作区文件'), [
			E('p', {}, _('将删除持久化工作区中的固件包、全量备份和任务日志，但保留工作区设置。请先确认备份已经下载到电脑。')),
			E('div', { 'class': 'fm-actions right' }, [
				E('button', { 'class': 'btn', 'click': ui.hideModal }, _('取消')),
				E('button', {
					'class': 'btn cbi-button-negative important',
					'click': L.bind(function() {
						ui.hideModal();
						this.runAndRefresh([ 'cleanup' ], _('刷写工作区文件已清理。'));
					}, this)
				}, _('确认清理'))
			])
		]);
	},

	renderFirmwareLibrary: function(firmwares, activeId, busy, workspaceReady, flashReady) {
		if (!this.firmwareList)
			return;
		const signature = JSON.stringify([ activeId, busy, workspaceReady, flashReady, firmwares ]);
		if (signature === this.firmwareSignature)
			return;
		this.firmwareSignature = signature;
		if (!firmwares.length) {
			this.firmwareList.replaceChildren(E('div', { 'class': 'fm-firmware-empty' }, [
				E('strong', {}, _('固件库为空')),
				E('span', {}, _('可以连续上传多个压缩包，再选择其中一个适用于本机 FM350 的版本进行备份或刷写。'))
			]));
			return;
		}

		const cards = firmwares.map(L.bind(function(pkg) {
			const accepted = pkg.state === 'accepted';
			const active = pkg.id === activeId || pkg.active;
			const stateText = active ? _('当前待刷') :
				(accepted ? _('可用于本机') : (pkg.state === 'validating' ? _('正在识别') : _('不可用于本机')));
			const flash = E('button', {
				'class': 'btn cbi-button-action',
				'disabled': busy || !accepted ? '' : null,
				'title': !accepted ? _('这个压缩包不是本机可用固件') :
					(!flashReady ? _('需要全量备份、FM350 USB 和完整执行层均已就绪') : _('使用这个 ZIP 启动刷写流程')),
				'click': ui.createHandlerFn(this, 'handleFirmwareFlash', pkg.id, pkg.name)
			}, _('刷入此固件'));
			const download = E('button', {
				'class': 'btn cbi-button-positive',
				'disabled': busy || !workspaceReady || !pkg.downloadable ? '' : null,
				'click': ui.createHandlerFn(this, 'handleFirmwareDownload', pkg.id, pkg.name)
			}, pkg.downloadable ? _('下载原包') : _('无原包'));
			const remove = E('button', {
				'class': 'btn cbi-button-negative',
				'disabled': busy || !workspaceReady ? '' : null,
				'click': ui.createHandlerFn(this, 'handleFirmwareDelete', pkg.id, pkg.name)
			}, _('删除'));
			const version = pkg.build_version || pkg.build_time || '—';
			const updateNames = Array.isArray(pkg.update_names) ? pkg.update_names : [];
			const layout = accepted ? '%s / %s / %s'.format(
				pkg.platform || '—', pkg.storage || '—', updateNames.length) : '—';
			const detail = accepted && updateNames.length ?
				_('写入分区：') + updateNames.join('、') + _('；保留身份、NV 和校准数据') :
				(pkg.message || _('不是本机可用固件或文件不完整'));

			return E('article', { 'class': 'fm-firmware-item' + (active ? ' active' : '') }, [
				E('div', { 'class': 'fm-firmware-head' }, [
					E('div', { 'class': 'fm-firmware-title' }, [
						E('strong', {}, pkg.name || _('未命名固件')),
						E('span', {}, '%s · %s'.format(pkg.size_text || '—', pkg.created_at || '—'))
					]),
					E('span', { 'class': 'fm-badge fm-firmware-' + (active ? 'active' : (accepted ? 'accepted' : 'failed')) }, stateText)
				]),
				E('div', { 'class': 'fm-firmware-grid' }, [
					E('div', {}, [ E('span', {}, _('内部版本')), E('strong', {}, version) ]),
					E('div', {}, [ E('span', {}, _('平台 / 存储 / 更新分区')), E('strong', {}, layout) ]),
					E('div', {}, [ E('span', {}, _('SHA-256')), E('strong', { 'class': 'fm-mono' }, pkg.sha256 || '—') ])
				]),
				E('div', { 'class': 'fm-firmware-message ' + (accepted ? 'accepted' : 'failed') }, detail),
				E('div', { 'class': 'fm-actions fm-firmware-actions' }, [ flash, download, remove ])
			]);
		}, this));
		this.firmwareList.replaceChildren.apply(this.firmwareList, cards);
	},

	updateStatus: function(status) {
		this.lastStatus = status;
		const tool = status.tool || {};
		const modem = status.modem || {};
		const pkg = status.package;
		const firmwares = Array.isArray(status.firmwares) ? status.firmwares : [];
		const backup = status.backup;
		const job = status.job || {};
		const workspace = status.workspace || {};
		const busy = job.state === 'waiting' || job.state === 'running';
		const workspaceReady = !!workspace.ready;

		text('fm-tool', tool.ready ? _('已就绪') : _('依赖不完整'));
		text('fm-revision', tool.revision || '—');
		text('fm-modem', busy ? _('刷写模式') : (modem.present ? _('已检测到') : _('未检测到')));
		text('fm-usb', busy ? _('模组正在执行下载或重启任务') :
			(modem.usb_id ? '%s · %s'.format(modem.usb_id, modem.mode || '') : '—'));
		text('fm-power-mode', modem.power_mode || _('手动断电或重新插拔'));
		text('fm-workspace-state', workspaceReady ? _('已启用') : _('不可用'));
		text('fm-workspace-path', workspace.path);
		text('fm-workspace-reason', workspace.reason);
		text('fm-workspace-total', workspace.total_text);
		text('fm-workspace-free', workspace.free_text);
		if (this.workspaceInput && !this.workspacePathDirty &&
				document.activeElement !== this.workspaceInput)
			this.workspaceInput.value = workspace.path || '/fm350-workspace';

		const partitions = Array.isArray(workspace.partitions) ? workspace.partitions :
			(Array.isArray(workspace.mounts) ? workspace.mounts : []);
		const mountSignature = JSON.stringify(partitions.map(function(item) {
			return [ item.source, item.mountpoint, item.total, item.free, item.suitable, item.reason ];
		}));
		if (this.mountSelect && mountSignature !== this.mountSignature) {
			this.mountSignature = mountSignature;
			this.mountSelect.replaceChildren(E('option', { 'value': '' }, _('选择存储分区…')));
			partitions.forEach(L.bind(function(item) {
				const location = item.mounted ? item.mountpoint : _('未挂载');
				this.mountSelect.appendChild(E('option', {
					'value': item.workspace_path || '',
					'disabled': item.suitable ? null : 'disabled'
				}, '%s · %s · %s · %s · %s'.format(
					item.source || '—', location, item.filesystem || '—',
					item.total_text, item.reason || '—')));
			}, this));
		}
		const flashReady = workspaceReady && !!backup && !!tool.ready &&
			(!!modem.compatible || !!modem.automatic_power);
		this.renderFirmwareLibrary(firmwares, status.active_firmware_id || '', busy, workspaceReady, flashReady);
		text('fm-backup-name', backup ? backup.name : _('尚无全量备份'));
		text('fm-backup-sha', backup ? backup.sha256 : '—');
		text('fm-backup-source', backup ? backup.source : _('可在本机制作，也可导入已有备份'));
		text('fm-job-message', job.message || _('当前没有任务'));
		text('fm-job-state', ({
			'idle': _('空闲'), 'waiting': _('等待'), 'running': _('运行中'),
			'completed': _('已完成'), 'failed': _('失败')
		})[job.state] || job.state || _('空闲'));

		const progress = document.getElementById('fm-job-progress');
		const progressValue = Math.max(0, Math.min(100, Number(job.progress) || 0));
		if (progress) {
			progress.setAttribute('title', '%d%%'.format(progressValue));
			progress.firstElementChild.style.width = '%d%%'.format(progressValue);
		}
		text('fm-job-percent', '%d%%'.format(progressValue));

		const log = document.getElementById('fm-log');
		if (log) {
			const logLines = visibleJobLogs(Array.isArray(job.logs) ? job.logs : []);
			/* Newest entry stays at the top; older entries are pushed downward. */
			log.textContent = logLines.length ? logLines.slice().reverse().join('\n') : _('暂无任务日志');
			log.scrollTop = 0;
		}

		if (this.buttons) {
			this.buttons.workspace.disabled = busy;
			this.buttons.firmware.disabled = busy || !workspaceReady;
			this.buttons.backupUpload.disabled = busy || !workspaceReady;
			this.buttons.backupStart.disabled = busy || !workspaceReady || !pkg || !tool.ready || !modem.compatible;
			this.buttons.backupDownload.disabled = busy || !workspaceReady || !backup;
			this.buttons.backupDelete.disabled = busy || !workspaceReady || !backup;
			this.buttons.restart.disabled = busy || !modem.automatic_power;
			this.buttons.cleanup.disabled = busy || !workspaceReady;
		}

		const workspaceBadge = document.getElementById('fm-workspace-state');
		if (workspaceBadge)
			workspaceBadge.className = 'fm-badge ' + (workspaceReady ? 'fm-state-completed' : 'fm-state-failed');

		const stateBadge = document.getElementById('fm-job-state');
		if (stateBadge)
			stateBadge.className = 'fm-badge fm-state-' + (job.state || 'idle');
	},

	render: function(status) {
		this.workspaceInput = E('input', {
			'class': 'cbi-input-text',
			'type': 'text',
			'autocomplete': 'off',
			'spellcheck': 'false',
			'input': L.bind(function() { this.workspacePathDirty = true; }, this),
			'value': status && status.workspace && status.workspace.path || '/fm350-workspace',
			'placeholder': '/mnt/disk/fm350-workspace'
		});
		this.mountSelect = E('select', {
			'class': 'cbi-input-select',
			'change': L.bind(this.handleMountSelect, this)
		}, [ E('option', { 'value': '' }, _('选择存储分区…')) ]);
		this.buttons = {
			workspace: E('button', { 'class': 'btn cbi-button-positive', 'click': ui.createHandlerFn(this, 'handleSetWorkspace') }, _('检测并启用工作区')),
			firmware: E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'handleFirmwareUpload') }, _('上传固件包')),
			backupUpload: E('button', { 'class': 'btn', 'click': ui.createHandlerFn(this, 'handleBackupUpload') }, _('导入已有全量备份')),
			backupStart: E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'handleStartBackup') }, _('制作本机全量备份')),
			backupDownload: E('button', { 'class': 'btn cbi-button-positive', 'click': ui.createHandlerFn(this, 'handleBackupDownload') }, _('下载全量备份')),
			backupDelete: E('button', { 'class': 'btn cbi-button-negative', 'click': ui.createHandlerFn(this, 'handleBackupDelete') }, _('删除全量备份')),
			restart: E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'handleRestartModem') }, _('重启模组')),
			cleanup: E('button', { 'class': 'btn', 'click': ui.createHandlerFn(this, 'handleCleanup') }, _('清理工作区'))
		};
		this.firmwareList = E('div', { 'class': 'fm-firmware-list' });

		const root = E('div', { 'class': 'fm-page' }, [
			E('div', { 'class': 'fm-title-row' }, [
				E('div', {}, [
					E('h2', {}, _('FM350-GL 固件备份与刷写')),
					E('p', { 'class': 'fm-subtitle' }, _('独立 LuCI 工具 · MT6880 签名 Stock DA · 标准 FLASH-UPDATE'))
				]),
				this.buttons.cleanup
			]),
			E('div', { 'class': 'alert-message warning fm-warning' }, [
				E('strong', {}, _('适用于通过 USB 连接的 Fibocom FM350-GL。')),
				' ', _('有 WWAN rfkill 的设备自动切换模组电源；其他 OpenWrt 设备按任务日志手动断电或重插。固件和备份必须保存在容量不小于 2 GiB 的持久化工作区。')
			]),
			E('div', { 'class': 'fm-status-grid' }, [
				E('div', { 'class': 'fm-stat' }, [ E('span', {}, _('执行层')), E('strong', { 'id': 'fm-tool' }), E('small', {}, [ _('版本 '), E('span', { 'id': 'fm-revision' }) ]) ]),
				E('div', { 'class': 'fm-stat' }, [ E('span', {}, _('FM350 USB')), E('strong', { 'id': 'fm-modem' }), E('small', {}, [ E('span', { 'id': 'fm-usb' }), ' · ', E('span', { 'id': 'fm-power-mode' }) ]) ]),
				E('div', { 'class': 'fm-stat' }, [ E('span', {}, _('持久化工作区')), E('strong', { 'id': 'fm-workspace-state', 'class': 'fm-badge fm-state-failed' }), E('small', { 'id': 'fm-workspace-path' }) ])
			]),

			E('section', { 'class': 'fm-card fm-workspace-card' }, [
				E('div', { 'class': 'fm-card-head' }, [
					E('div', {}, [ E('h3', {}, _('工作区设置')), E('p', {}, _('扫描全部存储分区；选择后只创建专用目录，不格式化、不重新分区、不修改挂载点')) ])
				]),
				E('div', { 'class': 'fm-workspace-form' }, [
					E('label', {}, [ E('span', {}, _('存储分区')), this.mountSelect ]),
					E('label', { 'class': 'fm-workspace-path-input' }, [ E('span', {}, _('工作区绝对路径')), this.workspaceInput ]),
					this.buttons.workspace
				]),
				E('div', { 'class': 'fm-workspace-summary' }, [
					E('span', {}, [ _('检测结果：'), E('strong', { 'id': 'fm-workspace-reason' }) ]),
					E('span', {}, [ _('文件系统容量：'), E('strong', { 'id': 'fm-workspace-total' }) ]),
					E('span', {}, [ _('当前可用：'), E('strong', { 'id': 'fm-workspace-free' }) ])
				])
			]),

			E('section', { 'class': 'fm-card' }, [
				E('div', { 'class': 'fm-card-head' }, [
					E('div', {}, [ E('h3', {}, _('1. 固件库')), E('p', {}, _('可上传并保留多个 ZIP、TAR、TAR.GZ、TAR.XZ 固件包；选择一个适用于本机 FM350 的版本作为待刷固件')) ]),
					this.buttons.firmware
				]),
				this.firmwareList
			]),

			E('section', { 'class': 'fm-card' }, [
				E('div', { 'class': 'fm-card-head' }, [ E('div', {}, [ E('h3', {}, _('2. 准备全量备份')), E('p', {}, _('精确回读 0x1fefffff 字节，SHA-256 校验后才能刷写')) ]) ]),
				E('div', { 'class': 'fm-detail-grid' }, [
					E('div', {}, [ E('span', {}, _('备份文件')), E('strong', { 'id': 'fm-backup-name' }) ]),
					E('div', { 'class': 'fm-wide' }, [ E('span', {}, _('SHA-256')), E('strong', { 'id': 'fm-backup-sha', 'class': 'fm-mono' }) ]),
					E('div', {}, [ E('span', {}, _('来源')), E('strong', { 'id': 'fm-backup-source' }) ])
				]),
				E('div', { 'class': 'fm-actions' }, [ this.buttons.backupStart, this.buttons.backupUpload, this.buttons.backupDownload, this.buttons.backupDelete ])
			]),

			E('section', { 'class': 'fm-card' }, [
				E('div', { 'class': 'fm-card-head' }, [
					E('div', {}, [ E('h3', {}, _('3. 刷写任务')), E('p', {}, _('一次进入下载模式，由签名 DA 按 Scatter 连续写入全部 UPDATE 分区；不写入 IMEI、SN、NV 和校准数据')) ]),
					this.buttons.restart
				]),
				E('div', { 'class': 'fm-job-line' }, [ E('span', { 'id': 'fm-job-state', 'class': 'fm-badge fm-state-idle' }), E('strong', { 'id': 'fm-job-message' }), E('span', { 'id': 'fm-job-percent' }) ]),
				E('div', { 'id': 'fm-job-progress', 'class': 'cbi-progressbar fm-progress', 'title': '0%' }, E('div', { 'style': 'width:0%' })),
				E('pre', { 'id': 'fm-log', 'class': 'fm-log' }, _('暂无任务日志'))
			])
		]);

		window.requestAnimationFrame(L.bind(function() { this.updateStatus(status || {}); }, this));
		poll.add(L.bind(this.refreshStatus, this), 2);
		return root;
	},

	handleSave: null,
	handleSaveApply: null,
	handleReset: null
});
