'use strict';

let fs = require('fs');
let path = ARGV[0];

if (!path)
	die('xm14pro-runc-compat: missing OCI config path\n');

let source = fs.readfile(path);
let spec = json(source);

if (type(spec) != 'object')
	die('xm14pro-runc-compat: invalid OCI config\n');

if (type(spec.linux) == 'object' && type(spec.linux.namespaces) == 'array') {
	spec.linux.namespaces = filter(spec.linux.namespaces, function(ns) {
		return ns.type != 'pid' && ns.type != 'ipc';
	});
}

if (type(spec.mounts) == 'array') {
	spec.mounts = filter(spec.mounts, function(mount) {
		return mount.destination != '/dev/mqueue';
	});
}

if (!fs.writefile(path, sprintf('%J\n', spec)))
	die('xm14pro-runc-compat: unable to write OCI config\n');
