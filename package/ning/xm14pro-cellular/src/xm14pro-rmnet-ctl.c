// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi 14 Pro downstream IPA/RMNET compatibility helper.
 *
 * This reproduces only the physical rmnet operations performed by the stock
 * OS3.0.308.0 NICM service before a WDS data call. It never starts a PDP
 * context and contains no SIM, operator or APN policy.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define RMNET_IOCTL_SET_LLP_IP                  0x000089F2
#define RMNET_IOCTL_OPEN                        0x000089F8
#define RMNET_IOCTL_EXTENDED                    0x000089FD
#define RMNET_IOCTL_EXTENDED_V2                 0x000089FE

#define RMNET_IOCTL_GET_SUPPORTED_FEATURES      0x0000
#define RMNET_IOCTL_SET_MRU                     0x0001
#define RMNET_IOCTL_ADD_MUX_CHANNEL             0x0005
#define RMNET_IOCTL_FEAT_NOTIFY_MUX_CHANNEL     (1U << 0)

#define RMNET_IOCTL_SET_EGRESS_DATA_FORMAT_V2   0x0000
#define RMNET_IOCTL_SET_INGRESS_DATA_FORMAT_V2  0x0001

#define STOCK_MRU                               64844U

/* Qualcomm extends the upstream rmnet link attributes locally.  In the
 * stock Xiaomi kernel __IFLA_RMNET_MAX is 3 and the first private attribute
 * is the 20-byte tcmsg used to set up its in-kernel DFC/WDA QMI clients. */
#define IFLA_RMNET_DFC_QOS                      3
#define RMNET_NLMSG_CLIENT_SETUP                4
#define STOCK_DFC_FLAGS                         0x74U
#define STOCK_QMI_INSTANCE                      0U
#define STOCK_EP_TYPE                           1U
#define STOCK_IFACE_ID                          1U
#define STOCK_DATA_FORMAT                       0xbc000071U
#define STOCK_NICM_AUX_DATA_FORMAT              0xb4000071U
#define NETLINK_BUFFER_SIZE                     1024U

#ifndef NLA_TYPE_MASK
#define NLA_TYPE_MASK                           0x3fff
#endif

struct rmnet_ioctl_extended {
	uint32_t extended_ioctl;
	union {
		uint32_t data;
		struct {
			uint32_t mux_id;
			int8_t vchannel_name[IFNAMSIZ];
		} rmnet_mux_val;
	} u;
};

struct rmnet_egress_param {
	uint32_t type;
	uint32_t status;
	uint32_t checksum;
	uint32_t aggregation;
	uint32_t ulso;
	uint32_t ipid_index;
	uint32_t int_modt;
	uint32_t int_modc;
};

struct rmnet_ingress_param {
	uint32_t type;
	uint32_t status;
	uint32_t checksum;
	uint32_t buffer_size;
	uint32_t aggregation_bytes;
	uint32_t aggregation_time;
	uint32_t aggregation_packets;
	uint32_t int_modt;
	uint32_t int_modc;
	uint32_t padding;
};

struct rmnet_format_v2 {
	uint64_t params;
	uint32_t param_size;
	uint32_t n_params;
};

struct rmnet_ioctl_extended_v2 {
	uint64_t data;
	uint32_t data_size;
	uint32_t type;
};

struct rtnl_request {
	struct nlmsghdr nlh;
	struct ifinfomsg ifm;
	uint8_t attributes[NETLINK_BUFFER_SIZE];
};

static int
add_rtnl_attr(struct nlmsghdr *nlh, size_t maximum, uint16_t type,
	      const void *data, size_t size)
{
	size_t length = RTA_LENGTH(size);
	size_t aligned = NLMSG_ALIGN(nlh->nlmsg_len);
	struct rtattr *attr;

	if (aligned + RTA_ALIGN(length) > maximum) {
		errno = ENOBUFS;
		return -1;
	}

	attr = (struct rtattr *)((uint8_t *)nlh + aligned);
	attr->rta_type = type;
	attr->rta_len = length;
	if (size)
		memcpy(RTA_DATA(attr), data, size);
	nlh->nlmsg_len = aligned + RTA_ALIGN(length);
	return 0;
}

static struct rtattr *
begin_rtnl_nest(struct nlmsghdr *nlh, size_t maximum, uint16_t type)
{
	struct rtattr *nest;

	nest = (struct rtattr *)((uint8_t *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
	if (add_rtnl_attr(nlh, maximum, type, NULL, 0) < 0)
		return NULL;
	return nest;
}

static void
end_rtnl_nest(struct nlmsghdr *nlh, struct rtattr *nest)
{
	nest->rta_len = (uint16_t)((uint8_t *)nlh + nlh->nlmsg_len -
				    (uint8_t *)nest);
}

static int
read_rtnl_ack(int fd, uint32_t sequence)
{
	uint8_t buffer[4096];
	ssize_t received;
	struct nlmsghdr *nlh;

	for (;;) {
		received = recv(fd, buffer, sizeof(buffer), 0);
		if (received < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "rtnetlink receive: %s\n", strerror(errno));
			return -1;
		}

		for (nlh = (struct nlmsghdr *)buffer;
		     NLMSG_OK(nlh, (unsigned int)received);
		     nlh = NLMSG_NEXT(nlh, received)) {
			struct nlmsgerr *error;

			if (nlh->nlmsg_seq != sequence)
				continue;
			if (nlh->nlmsg_type != NLMSG_ERROR)
				continue;
			if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*error))) {
				fprintf(stderr, "short rtnetlink acknowledgement\n");
				return -1;
			}
			error = NLMSG_DATA(nlh);
			if (error->error) {
				errno = -error->error;
				fprintf(stderr, "rtnetlink client setup: %s\n",
					strerror(errno));
				return -1;
			}
			return 0;
		}
	}
}

static int
query_data_format(const char *ifname)
{
	struct {
		struct nlmsghdr nlh;
		struct ifinfomsg ifm;
	} request;
	struct sockaddr_nl peer;
	uint8_t buffer[4096];
	unsigned int ifindex;
	uint32_t sequence = 1;
	int fd;

	ifindex = if_nametoindex(ifname);
	if (!ifindex) {
		fprintf(stderr, "cannot resolve interface %s: %s\n",
			ifname, strerror(errno));
		return -1;
	}

	memset(&request, 0, sizeof(request));
	request.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(request.ifm));
	request.nlh.nlmsg_type = RTM_GETLINK;
	request.nlh.nlmsg_flags = NLM_F_REQUEST;
	request.nlh.nlmsg_seq = sequence;
	request.ifm.ifi_family = AF_UNSPEC;
	request.ifm.ifi_index = (int)ifindex;

	fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (fd < 0) {
		fprintf(stderr, "rtnetlink socket: %s\n", strerror(errno));
		return -1;
	}
	memset(&peer, 0, sizeof(peer));
	peer.nl_family = AF_NETLINK;
	if (sendto(fd, &request, request.nlh.nlmsg_len, 0,
		   (struct sockaddr *)&peer, sizeof(peer)) < 0) {
		fprintf(stderr, "rtnetlink link query send: %s\n",
			strerror(errno));
		close(fd);
		return -1;
	}

	for (;;) {
		ssize_t received;
		struct nlmsghdr *nlh;

		received = recv(fd, buffer, sizeof(buffer), 0);
		if (received < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "rtnetlink link query receive: %s\n",
				strerror(errno));
			close(fd);
			return -1;
		}

		for (nlh = (struct nlmsghdr *)buffer;
		     NLMSG_OK(nlh, (unsigned int)received);
		     nlh = NLMSG_NEXT(nlh, received)) {
			struct ifinfomsg *ifm;
			struct rtattr *attr;
			int attr_len;

			if (nlh->nlmsg_seq != sequence)
				continue;
			if (nlh->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *error = NLMSG_DATA(nlh);

				errno = error->error ? -error->error : EPROTO;
				fprintf(stderr, "rtnetlink link query: %s\n",
					strerror(errno));
				close(fd);
				return -1;
			}
			if (nlh->nlmsg_type != RTM_NEWLINK)
				continue;

			ifm = NLMSG_DATA(nlh);
			if ((unsigned int)ifm->ifi_index != ifindex)
				continue;
			attr = IFLA_RTA(ifm);
			attr_len = IFLA_PAYLOAD(nlh);
			for (; RTA_OK(attr, attr_len);
			     attr = RTA_NEXT(attr, attr_len)) {
				struct rtattr *link_attr;
				int link_len;

				if ((attr->rta_type & NLA_TYPE_MASK) != IFLA_LINKINFO)
					continue;
				link_attr = RTA_DATA(attr);
				link_len = RTA_PAYLOAD(attr);
				for (; RTA_OK(link_attr, link_len);
				     link_attr = RTA_NEXT(link_attr, link_len)) {
					struct rtattr *data_attr;
					int data_len;

					if ((link_attr->rta_type & NLA_TYPE_MASK) !=
					    IFLA_INFO_DATA)
						continue;
					data_attr = RTA_DATA(link_attr);
					data_len = RTA_PAYLOAD(link_attr);
					for (; RTA_OK(data_attr, data_len);
					     data_attr = RTA_NEXT(data_attr, data_len)) {
						struct ifla_rmnet_flags *flags;

						if ((data_attr->rta_type & NLA_TYPE_MASK) !=
						    IFLA_RMNET_FLAGS)
							continue;
						if (RTA_PAYLOAD(data_attr) < sizeof(*flags)) {
							fprintf(stderr, "short rmnet flags attribute\n");
							close(fd);
							return -1;
						}
						flags = RTA_DATA(data_attr);
						printf("flags=0x%08x mask=0x%08x\n",
						       flags->flags, flags->mask);
						close(fd);
						return 0;
					}
				}
			}
			fprintf(stderr, "%s has no rmnet flags attribute\n", ifname);
			close(fd);
			return -1;
		}
	}
}

static int
setup_qmi_clients(const char *parent_name, const char *ifname)
{
	struct rtnl_request request;
	struct rtattr *linkinfo;
	struct rtattr *infodata;
	struct ifla_rmnet_flags data_format;
	struct tcmsg tcm;
	struct sockaddr_nl peer;
	unsigned int parent_ifindex;
	unsigned int ifindex;
	uint32_t sequence = 1;
	int fd;

	parent_ifindex = if_nametoindex(parent_name);
	if (!parent_ifindex) {
		fprintf(stderr, "cannot resolve parent interface %s: %s\n",
			parent_name, strerror(errno));
		return -1;
	}
	ifindex = if_nametoindex(ifname);
	if (!ifindex) {
		fprintf(stderr, "cannot resolve interface %s: %s\n",
			ifname, strerror(errno));
		return -1;
	}

	memset(&request, 0, sizeof(request));
	request.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(request.ifm));
	request.nlh.nlmsg_type = RTM_NEWLINK;
	request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	request.nlh.nlmsg_seq = sequence;
	request.ifm.ifi_family = AF_UNSPEC;
	request.ifm.ifi_index = (int)ifindex;

	/* librmnetctl identifies both ends explicitly when changing an existing
	 * VND.  The downstream rmnet driver reads IFLA_LINK while dispatching
	 * the private DFC_QOS message; an ifi_index on its own is not enough. */
	if (add_rtnl_attr(&request.nlh, sizeof(request), IFLA_LINK,
			  &parent_ifindex, sizeof(parent_ifindex)) < 0 ||
	    add_rtnl_attr(&request.nlh, sizeof(request), IFLA_IFNAME,
			  ifname, strlen(ifname) + 1) < 0)
		goto too_large;

	linkinfo = begin_rtnl_nest(&request.nlh, sizeof(request), IFLA_LINKINFO);
	if (!linkinfo ||
	    add_rtnl_attr(&request.nlh, sizeof(request), IFLA_INFO_KIND,
			  "rmnet", sizeof("rmnet")) < 0)
		goto too_large;
	infodata = begin_rtnl_nest(&request.nlh, sizeof(request), IFLA_INFO_DATA);
	if (!infodata)
		goto too_large;

	/* Read from all four stock OS3.0.308.0 rmnet VNDs on this device.  The
	 * V2 DL marker bit must be set before WDA connects; otherwise MPSS has no
	 * downlink SIO-to-IPA mapping and asserts when WDS starts the data call. */
	data_format.flags = STOCK_DATA_FORMAT;
	data_format.mask = UINT32_MAX;
	if (add_rtnl_attr(&request.nlh, sizeof(request), IFLA_RMNET_FLAGS,
			  &data_format, sizeof(data_format)) < 0)
		goto too_large;

	/* Exact values used by OS3.0.308.0 on shennong:
	 * KFC mode 4, QMAP enabled, power-save and extended power-save; QMI
	 * instance 0 on the embedded endpoint/interface pair 1/1. */
	memset(&tcm, 0, sizeof(tcm));
	tcm.tcm_family = RMNET_NLMSG_CLIENT_SETUP;
	tcm.tcm_ifindex = STOCK_DFC_FLAGS;
	tcm.tcm_handle = STOCK_QMI_INSTANCE;
	tcm.tcm_parent = STOCK_IFACE_ID;
	tcm.tcm_info = STOCK_EP_TYPE;
	if (add_rtnl_attr(&request.nlh, sizeof(request), IFLA_RMNET_DFC_QOS,
			  &tcm, sizeof(tcm)) < 0)
		goto too_large;
	end_rtnl_nest(&request.nlh, infodata);
	end_rtnl_nest(&request.nlh, linkinfo);

	fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (fd < 0) {
		fprintf(stderr, "rtnetlink socket: %s\n", strerror(errno));
		return -1;
	}
	memset(&peer, 0, sizeof(peer));
	peer.nl_family = AF_NETLINK;
	if (sendto(fd, &request, request.nlh.nlmsg_len, 0,
		   (struct sockaddr *)&peer, sizeof(peer)) < 0) {
		fprintf(stderr, "rtnetlink client setup send: %s\n",
			strerror(errno));
		close(fd);
		return -1;
	}
	if (read_rtnl_ack(fd, sequence) < 0) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;

too_large:
	fprintf(stderr, "rtnetlink client setup message is too large\n");
	return -1;
}

static int
open_control_socket(void)
{
	int fd;

	fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		fprintf(stderr, "socket: %s\n", strerror(errno));
	return fd;
}

static void
init_ifreq(struct ifreq *ifr, const char *ifname, void *data)
{
	memset(ifr, 0, sizeof(*ifr));
	if (strlen(ifname) >= IFNAMSIZ) {
		fprintf(stderr, "interface name is too long: %s\n", ifname);
		exit(2);
	}
	strncpy(ifr->ifr_name, ifname, IFNAMSIZ - 1);
	ifr->ifr_data = data;
}

static int
run_simple_ioctl(int fd, const char *ifname, unsigned long request)
{
	struct ifreq ifr;

	init_ifreq(&ifr, ifname, NULL);
	if (ioctl(fd, request, &ifr) < 0) {
		fprintf(stderr, "%s ioctl 0x%lx: %s\n",
			ifname, request, strerror(errno));
		return -1;
	}
	return 0;
}

static int
run_extended_ioctl(int fd, const char *ifname,
			   struct rmnet_ioctl_extended *ext)
{
	struct ifreq ifr;

	init_ifreq(&ifr, ifname, ext);
	if (ioctl(fd, RMNET_IOCTL_EXTENDED, &ifr) < 0) {
		fprintf(stderr, "%s extended ioctl %u: %s\n",
			ifname, ext->extended_ioctl, strerror(errno));
		return -1;
	}
	return 0;
}

static int
configure_v2(int fd, const char *ifname)
{
	struct rmnet_egress_param egress[] = {
		{ 0, 0, 1, 1, 1, 0, 16, 20 },
		{ 1, 0, 0, 0, 0, 0, 16,  1 },
		{ 2, 0, 1, 0, 0, 0, 16, 20 },
	};
	struct rmnet_ingress_param ingress[] = {
		{ 0, 0, 1, 32768, 64844, 500, 63, 16, 20, 0 },
		{ 1, 0, 1, 32768, 64844, 500, 63, 16, 20, 0 },
		{ 2, 0, 0,  4096,     0,   0,  0, 16,  1, 0 },
		{ 3, 0, 1,  4096,     0,   0,  1, 16, 20, 0 },
	};
	struct rmnet_format_v2 format;
	struct rmnet_ioctl_extended_v2 v2;
	struct ifreq ifr;
	size_t i;

	memset(&format, 0, sizeof(format));
	format.params = (uint64_t)(uintptr_t)egress;
	format.param_size = sizeof(egress[0]);
	format.n_params = sizeof(egress) / sizeof(egress[0]);
	memset(&v2, 0, sizeof(v2));
	v2.data = (uint64_t)(uintptr_t)&format;
	v2.data_size = sizeof(format);
	v2.type = RMNET_IOCTL_SET_EGRESS_DATA_FORMAT_V2;
	init_ifreq(&ifr, ifname, &v2);
	if (ioctl(fd, RMNET_IOCTL_EXTENDED_V2, &ifr) < 0) {
		fprintf(stderr, "%s V2 egress: %s\n", ifname, strerror(errno));
		return -1;
	}
	for (i = 0; i < sizeof(egress) / sizeof(egress[0]); i++) {
		if (egress[i].status != 0 && egress[i].status != 2) {
			fprintf(stderr, "%s V2 egress type %u failed: status %u\n",
				ifname, egress[i].type, egress[i].status);
			return -1;
		}
	}

	memset(&format, 0, sizeof(format));
	format.params = (uint64_t)(uintptr_t)ingress;
	format.param_size = sizeof(ingress[0]);
	format.n_params = sizeof(ingress) / sizeof(ingress[0]);
	memset(&v2, 0, sizeof(v2));
	v2.data = (uint64_t)(uintptr_t)&format;
	v2.data_size = sizeof(format);
	v2.type = RMNET_IOCTL_SET_INGRESS_DATA_FORMAT_V2;
	init_ifreq(&ifr, ifname, &v2);
	if (ioctl(fd, RMNET_IOCTL_EXTENDED_V2, &ifr) < 0) {
		fprintf(stderr, "%s V2 ingress: %s\n", ifname, strerror(errno));
		return -1;
	}
	for (i = 0; i < sizeof(ingress) / sizeof(ingress[0]); i++) {
		if (ingress[i].status != 0 && ingress[i].status != 2) {
			fprintf(stderr, "%s V2 ingress type %u failed: status %u\n",
				ifname, ingress[i].type, ingress[i].status);
			return -1;
		}
	}

	return 0;
}

static int
configure_parent(const char *ifname)
{
	struct rmnet_ioctl_extended ext;
	int fd;
	int rc = -1;

	fd = open_control_socket();
	if (fd < 0)
		return -1;

	if (run_simple_ioctl(fd, ifname, RMNET_IOCTL_SET_LLP_IP) < 0)
		goto out;

	memset(&ext, 0, sizeof(ext));
	ext.extended_ioctl = RMNET_IOCTL_SET_MRU;
	ext.u.data = STOCK_MRU;
	if (run_extended_ioctl(fd, ifname, &ext) < 0)
		goto out;

	if (run_simple_ioctl(fd, ifname, RMNET_IOCTL_OPEN) < 0)
		goto out;

	if (configure_v2(fd, ifname) < 0)
		goto out;

	rc = 0;
out:
	close(fd);
	return rc;
}

static int
notify_mux(const char *ifname, const char *link_name, unsigned long mux_id)
{
	struct rmnet_ioctl_extended ext;
	int fd;
	int rc = -1;

	if (mux_id == 0 || mux_id > 254 || strlen(link_name) >= IFNAMSIZ) {
		fprintf(stderr, "invalid mux mapping: %s/%lu\n", link_name, mux_id);
		return -1;
	}

	fd = open_control_socket();
	if (fd < 0)
		return -1;

	memset(&ext, 0, sizeof(ext));
	ext.extended_ioctl = RMNET_IOCTL_GET_SUPPORTED_FEATURES;
	if (run_extended_ioctl(fd, ifname, &ext) < 0)
		goto out;
	if (!(ext.u.data & RMNET_IOCTL_FEAT_NOTIFY_MUX_CHANNEL)) {
		fprintf(stderr, "%s does not support mux notification\n", ifname);
		goto out;
	}

	memset(&ext, 0, sizeof(ext));
	ext.extended_ioctl = RMNET_IOCTL_ADD_MUX_CHANNEL;
	ext.u.rmnet_mux_val.mux_id = (uint32_t)mux_id;
	strncpy((char *)ext.u.rmnet_mux_val.vchannel_name,
		link_name, sizeof(ext.u.rmnet_mux_val.vchannel_name) - 1);
	if (run_extended_ioctl(fd, ifname, &ext) < 0)
		goto out;

	rc = 0;
out:
	close(fd);
	return rc;
}

static int
prepare_nicm_aux_link(const char *parent_name, const char *link_name,
		      unsigned long mux_id)
{
	struct rtnl_request request;
	struct rtattr *linkinfo;
	struct rtattr *infodata;
	struct ifla_rmnet_flags data_format;
	struct sockaddr_nl peer;
	struct ifreq ifr;
	unsigned int parent_ifindex;
	unsigned int link_ifindex;
	uint32_t tx_queues = 31;
	uint32_t rx_queues = 1;
	uint16_t mux_id_u16;
	uint32_t sequence = 1;
	int fd;

	if (mux_id == 0 || mux_id > 254 || strlen(link_name) >= IFNAMSIZ) {
		fprintf(stderr, "invalid NICM auxiliary mux mapping: %s/%lu\n",
			link_name, mux_id);
		return -1;
	}

	parent_ifindex = if_nametoindex(parent_name);
	if (!parent_ifindex) {
		fprintf(stderr, "cannot resolve parent interface %s: %s\n",
			parent_name, strerror(errno));
		return -1;
	}

	/* A retry in the same boot must not recreate or register an already live
	 * downstream IPA mux.  The link is created only by this helper on nwrt. */
	link_ifindex = if_nametoindex(link_name);
	if (link_ifindex)
		return 0;

	memset(&request, 0, sizeof(request));
	request.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(request.ifm));
	request.nlh.nlmsg_type = RTM_NEWLINK;
	request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK |
		NLM_F_CREATE | NLM_F_EXCL;
	request.nlh.nlmsg_seq = sequence;
	request.ifm.ifi_family = AF_UNSPEC;

	if (add_rtnl_attr(&request.nlh, sizeof(request), IFLA_NUM_TX_QUEUES,
			  &tx_queues, sizeof(tx_queues)) < 0 ||
	    add_rtnl_attr(&request.nlh, sizeof(request), IFLA_NUM_RX_QUEUES,
			  &rx_queues, sizeof(rx_queues)) < 0 ||
	    add_rtnl_attr(&request.nlh, sizeof(request), IFLA_LINK,
			  &parent_ifindex, sizeof(parent_ifindex)) < 0 ||
	    add_rtnl_attr(&request.nlh, sizeof(request), IFLA_IFNAME,
			  link_name, strlen(link_name) + 1) < 0)
		goto too_large;

	linkinfo = begin_rtnl_nest(&request.nlh, sizeof(request), IFLA_LINKINFO);
	if (!linkinfo ||
	    add_rtnl_attr(&request.nlh, sizeof(request), IFLA_INFO_KIND,
			  "rmnet", sizeof("rmnet")) < 0)
		goto too_large;
	infodata = begin_rtnl_nest(&request.nlh, sizeof(request), IFLA_INFO_DATA);
	if (!infodata)
		goto too_large;

	mux_id_u16 = (uint16_t)mux_id;
	data_format.flags = STOCK_NICM_AUX_DATA_FORMAT;
	data_format.mask = STOCK_NICM_AUX_DATA_FORMAT;
	if (add_rtnl_attr(&request.nlh, sizeof(request), IFLA_RMNET_MUX_ID,
			  &mux_id_u16, sizeof(mux_id_u16)) < 0 ||
	    add_rtnl_attr(&request.nlh, sizeof(request), IFLA_RMNET_FLAGS,
			  &data_format, sizeof(data_format)) < 0)
		goto too_large;
	end_rtnl_nest(&request.nlh, infodata);
	end_rtnl_nest(&request.nlh, linkinfo);

	fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (fd < 0) {
		fprintf(stderr, "rtnetlink socket: %s\n", strerror(errno));
		return -1;
	}
	memset(&peer, 0, sizeof(peer));
	peer.nl_family = AF_NETLINK;
	if (sendto(fd, &request, request.nlh.nlmsg_len, 0,
		   (struct sockaddr *)&peer, sizeof(peer)) < 0) {
		fprintf(stderr, "rtnetlink NICM auxiliary link create: %s\n",
			strerror(errno));
		close(fd);
		return -1;
	}
	if (read_rtnl_ack(fd, sequence) < 0) {
		close(fd);
		return -1;
	}
	close(fd);

	fd = open_control_socket();
	if (fd < 0)
		return -1;
	init_ifreq(&ifr, link_name, NULL);
	ifr.ifr_mtu = 1500;
	if (ioctl(fd, SIOCSIFMTU, &ifr) < 0) {
		fprintf(stderr, "%s set MTU: %s\n", link_name, strerror(errno));
		close(fd);
		return -1;
	}
	init_ifreq(&ifr, link_name, NULL);
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		fprintf(stderr, "%s get flags: %s\n", link_name, strerror(errno));
		close(fd);
		return -1;
	}
	ifr.ifr_flags &= ~IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
		fprintf(stderr, "%s set down: %s\n", link_name, strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);

	/* Android registers this otherwise idle mux with IPA immediately after
	 * creating it and before allocating its auxiliary WDS/QOS clients. */
	return notify_mux(parent_name, link_name, mux_id);

too_large:
	fprintf(stderr, "rtnetlink NICM auxiliary link message is too large\n");
	return -1;
}

static void
usage(const char *program)
{
	fprintf(stderr,
		"usage: %s configure-parent IFNAME\n"
		"       %s query-data-format RMNET_LINK\n"
		"       %s notify-mux IFNAME LINK_NAME MUX_ID\n"
		"       %s setup-qmi-clients PARENT RMNET_LINK\n"
		"       %s prepare-nicm-aux PARENT LINK_NAME MUX_ID\n",
		program, program, program, program, program);
}

int
main(int argc, char **argv)
{
	char *end = NULL;
	unsigned long mux_id;

	if (argc == 3 && strcmp(argv[1], "configure-parent") == 0)
		return configure_parent(argv[2]) == 0 ? 0 : 1;

	if (argc == 3 && strcmp(argv[1], "query-data-format") == 0)
		return query_data_format(argv[2]) == 0 ? 0 : 1;

	if (argc == 4 && strcmp(argv[1], "setup-qmi-clients") == 0)
		return setup_qmi_clients(argv[2], argv[3]) == 0 ? 0 : 1;

	if (argc == 5 && strcmp(argv[1], "notify-mux") == 0) {
		errno = 0;
		mux_id = strtoul(argv[4], &end, 10);
		if (errno || !end || *end) {
			usage(argv[0]);
			return 2;
		}
		return notify_mux(argv[2], argv[3], mux_id) == 0 ? 0 : 1;
	}

	if (argc == 5 && strcmp(argv[1], "prepare-nicm-aux") == 0) {
		errno = 0;
		mux_id = strtoul(argv[4], &end, 10);
		if (errno || !end || *end) {
			usage(argv[0]);
			return 2;
		}
		return prepare_nicm_aux_link(argv[2], argv[3], mux_id) == 0 ? 0 : 1;
	}

	usage(argv[0]);
	return 2;
}
