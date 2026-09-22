'use strict';
'require view';
'require form';
'require fs';
'require uci';
'require ui';
'require poll';

const HELPER='/usr/libexec/remote-access';
function call(action){return fs.exec_direct(HELPER,[action],'json');}
function notify(result){ui.addNotification(null,E('p',{},result.message||_('操作完成')),result.ok?'info':'error');if(result.login_url)window.open(result.login_url,'_blank','noopener');return result;}

return view.extend({
	load:function(){return Promise.all([L.resolveDefault(call('status'),{}),uci.load('remote_access')]);},
	render:function(data){
		let m=new form.Map('remote_access',_('远程访问'),_('通过 Tailscale 建立端到端加密网络。通常无需公网 IP；直连失败时可能经中继转发。'));
		let st=data[0]||{},s=m.section(form.NamedSection,'main','main',_('连接与路由'));
		s.anonymous=true;s.addremove=false;
		let o=s.option(form.DummyValue,'_state',_('当前状态'));o.cfgvalue=()=>[st.state||'Stopped',st.ipv4,st.dns_name].filter(Boolean).join(' · ');
		o=s.option(form.Flag,'enabled',_('开机自动连接'));o.rmempty=false;
		o=s.option(form.Value,'hostname',_('设备名称'));o.placeholder=_('留空使用系统主机名');
		o=s.option(form.Value,'login_server',_('控制服务器'));o.placeholder='https://controlplane.tailscale.com';o.description=_('留空使用 Tailscale，也可填写自己的 Headscale 地址。');
		o=s.option(form.Flag,'accept_dns',_('接收远端 DNS'));
		o=s.option(form.Flag,'accept_routes',_('接收远端子网路由'));
		o=s.option(form.Value,'advertise_routes',_('发布本地网段'));o.placeholder='192.168.1.0/24';
		o=s.option(form.Flag,'advertise_exit_node',_('允许作为出口节点'));
		o=s.option(form.Value,'exit_node',_('使用出口节点'));o.placeholder=_('节点 IP 或名称，留空关闭');
		o=s.option(form.Flag,'exit_node_allow_lan_access',_('使用出口节点时仍可访问本地 LAN'));
		o=s.option(form.Flag,'tailscale_ssh',_('启用 Tailscale SSH'));
		s=m.section(form.TypedSection,'_actions',_('即时操作'));s.anonymous=true;s.render=()=>E('div',{'class':'cbi-section'},[
			E('h3',{},_('即时操作')),E('div',{'class':'cbi-section-node'},[
			E('button',{'class':'btn cbi-button-positive','click':ui.createHandlerFn(this,()=>call('connect').then(notify))},_('连接 / 获取登录地址')),' ',
			E('button',{'class':'btn cbi-button-action','click':ui.createHandlerFn(this,()=>call('apply').then(notify))},_('应用运行参数')),' ',
			E('button',{'class':'btn','click':ui.createHandlerFn(this,()=>call('down').then(notify))},_('暂停')),' ',
			E('button',{'class':'btn cbi-button-negative','click':ui.createHandlerFn(this,()=>call('logout').then(notify))},_('退出网络'))
		])]);
		return m.render();
	}
});
