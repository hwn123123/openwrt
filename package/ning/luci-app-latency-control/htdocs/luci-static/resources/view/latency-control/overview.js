'use strict';
'require view';
'require form';
'require uci';
'require rpc';
'require tools.widgets as widgets';

const service=rpc.declare({object:'rc',method:'init',params:['name','action']});

return view.extend({
	load:function(){return uci.load('sqm');},
	render:function(){
		let m=new form.Map('sqm',_('游戏 / 视频低延迟'),_('使用 CAKE 公平队列控制缓冲膨胀。上下行应填写稳定实测速度的 85%–95%，过高就无法压住延迟。'));
		let s=m.section(form.TypedSection,'queue',_('主线路'));s.anonymous=true;s.addremove=false;
		let o=s.option(form.Flag,'enabled',_('启用低延迟整形'));o.rmempty=false;
		o=s.option(widgets.DeviceSelect,'interface',_('上行接口'));o.rmempty=false;o.noaliases=true;
		o=s.option(form.Value,'download',_('下载上限 (kbit/s)'));o.datatype='uinteger';o.rmempty=false;
		o=s.option(form.Value,'upload',_('上传上限 (kbit/s)'));o.datatype='uinteger';o.rmempty=false;
		o=s.option(form.ListValue,'latency_preset',_('业务偏好'));o.value('balanced',_('均衡：游戏、视频和日常'));o.value('gaming',_('游戏优先：保留四级 DiffServ'));o.value('streaming',_('视频与会议：稳定吞吐'));o.default='balanced';
		o.write=function(section,value){uci.set('sqm',section,'latency_preset',value);uci.set('sqm',section,'qdisc','cake');uci.set('sqm',section,'script','layer_cake.qos');uci.set('sqm',section,'qdisc_advanced','1');uci.set('sqm',section,'iqdisc_opts',value==='streaming'?'besteffort nat dual-dsthost ingress':'diffserv4 nat dual-dsthost ingress');uci.set('sqm',section,'eqdisc_opts',value==='streaming'?'besteffort nat dual-srchost ack-filter':'diffserv4 nat dual-srchost ack-filter');};
		s=m.section(form.TypedSection,'_actions',_('服务控制'));s.anonymous=true;s.render=()=>E('div',{'class':'cbi-section'},[E('button',{'class':'btn cbi-button-positive','click':()=>service('sqm','restart').then(()=>location.reload())},_('重启并应用')), ' ',E('button',{'class':'btn','click':()=>service('sqm','stop').then(()=>location.reload())},_('停止整形'))]);
		return m.render();
	}
});
