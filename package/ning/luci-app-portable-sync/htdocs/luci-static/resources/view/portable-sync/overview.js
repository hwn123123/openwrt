'use strict';
'require view';
'require form';
'require uci';
'require rpc';
'require ui';

const service=rpc.declare({object:'rc',method:'init',params:['name','action']});

function guiURL(){let address=uci.get('syncthing','syncthing','gui_address')||'http://0.0.0.0:8384';return address.replace('0.0.0.0',location.hostname).replace('127.0.0.1',location.hostname);}

return view.extend({
	load:function(){return uci.load('syncthing');},
	render:function(){
		let m=new form.Map('syncthing',_('随身同步盘'),_('使用 Syncthing 在手机、电脑和路由器存储之间直接同步文件。文件不必经过公共云盘。'));
		let s=m.section(form.NamedSection,'syncthing','syncthing',_('服务设置'));s.anonymous=true;s.addremove=false;
		let o=s.option(form.Flag,'enabled',_('启用同步服务'));o.rmempty=false;
		o=s.option(form.Value,'gui_address',_('管理界面监听地址'));o.placeholder='http://0.0.0.0:8384';o.rmempty=false;
		o=s.option(form.Value,'home',_('配置与索引目录'));o.placeholder='/etc/syncthing';o.description=_('这里只保存数据库和设备配置；同步文件夹在 Syncthing 管理界面中添加。');
		o=s.option(form.Value,'user',_('运行用户'));o.placeholder='syncthing';
		o=s.option(form.Value,'group',_('运行用户组'));o.placeholder='syncthing';
		o=s.option(form.Value,'memlimit',_('内存软上限 (MB)'));o.datatype='uinteger';o.placeholder='0';
		s=m.section(form.TypedSection,'_actions',_('服务控制'));s.anonymous=true;s.render=()=>E('div',{'class':'cbi-section'},[
		E('button',{'class':'btn cbi-button-positive','click':()=>service('syncthing','restart').then(()=>ui.addNotification(null,E('p',{},_('同步服务已重启')),'info'))},_('重启并应用')),' ',
		E('button',{'class':'btn','click':()=>service('syncthing','stop')},_('停止')),' ',
		E('a',{'class':'btn cbi-button-action','href':guiURL(),'target':'_blank','rel':'noopener'},_('打开 Syncthing 管理界面'))
	]);
		return m.render();
	}
});
