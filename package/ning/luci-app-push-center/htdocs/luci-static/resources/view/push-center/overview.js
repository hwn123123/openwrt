'use strict';
'require view';
'require form';
'require fs';
'require uci';
'require ui';

const HELPER='/usr/libexec/push-center';

return view.extend({
	load:function(){return Promise.all([uci.load('push_center'),L.resolveDefault(fs.exec_direct(HELPER,['status'],'json'),{})]);},
	render:function(data){
		let m=new form.Map('push_center',_('手机推送中心'),_('把路由器事件发到 ntfy、Telegram 或自定义 Webhook。其他本机脚本也可调用 /usr/libexec/push-center send。'));
		let s=m.section(form.NamedSection,'main','main',_('推送服务'));s.anonymous=true;s.addremove=false;
		let o=s.option(form.Flag,'enabled',_('启用通知'));o.rmempty=false;
		o=s.option(form.ListValue,'provider',_('服务类型'));o.value('ntfy','ntfy');o.value('telegram','Telegram Bot');o.value('webhook','通用 Webhook');
		o=s.option(form.Value,'ntfy_server',_('ntfy 服务器'));o.depends('provider','ntfy');o.placeholder='https://ntfy.sh';
		o=s.option(form.Value,'ntfy_topic',_('ntfy 主题'));o.depends('provider','ntfy');o.description=_('使用难以猜测的长主题，或配置访问令牌。');
		o=s.option(form.Value,'ntfy_token',_('ntfy 访问令牌'));o.depends('provider','ntfy');o.password=true;
		o=s.option(form.Value,'telegram_bot_token',_('Bot Token'));o.depends('provider','telegram');o.password=true;
		o=s.option(form.Value,'telegram_chat_id',_('Chat ID'));o.depends('provider','telegram');
		o=s.option(form.Value,'webhook_url',_('Webhook 地址'));o.depends('provider','webhook');o.password=true;
		o=s.option(form.Flag,'event_boot',_('启动完成通知'));
		o=s.option(form.Flag,'event_wan',_('上行连接 / 断开通知'));
		o=s.option(form.Value,'wan_interfaces',_('上行逻辑接口'));o.placeholder='wan wwan wwan_fm350';
		s=m.section(form.TypedSection,'_test',_('连接测试'));s.anonymous=true;s.render=()=>E('div',{'class':'cbi-section'},[
		E('p',{},_('请先保存设置，再发送测试消息。')),
		E('button',{'class':'btn cbi-button-positive','click':ui.createHandlerFn(this,()=>fs.exec_direct(HELPER,['test'],'json').then(r=>ui.addNotification(null,E('p',{},r.message),r.ok?'info':'error')))},_('发送测试通知'))
	]);
		return m.render();
	}
});
