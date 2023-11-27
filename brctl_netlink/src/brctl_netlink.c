/**
 * brctl_netlink.c: 'brctl' implementation using Netlink sockets
 *
 * Copyright (c) 2023 Alexey Mikhailov. All rights reserved.
 *
 * This work is licensed under the terms of the MIT license.
 * For a copy, see <https://opensource.org/licenses/MIT>.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>

#include <net/if.h>

#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/socket.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "brctl_netlink.h"

static int debug = 1;
static pid_t pid;

/**
 * is_if_bridge: checks if given link name corresponds to bridge device
 *
 * @ifname: name of the link
 */

static int is_if_bridge (const char *ifname)
{
        char buf[1024];
        struct stat st;

	snprintf(buf, 1024, "/sys/devices/virtual/net/%s/bridge", ifname);

        if (stat(buf, &st) < 0)
		return -1;

	return 1;
}

static int cmd_show_if (const char *name)
{
        /* we're in /sys/class/net */
        /* BRCTL_FMT_STRING "%-14s%-24s%-16s%s\n" */
        /* /sys/devices/virtual/net/br0/bridge/bridge_id */
        /* br0/bridge/bridge_id */
        /* $ sudo ls -l /sys/class/net/br0/brif
lrwxrwxrwx 0 root root 26 ноя 22:49 dummy0 -> ../../dummy0/brport
lrwxrwxrwx 0 root root 26 ноя 23:02 dummy1 -> ../../dummy1/brport */
        /* $ sudo cat /sys/class/net/br0/bridge/stp_state 
           0
        */
        char pathbuf[PATH_MAX];
        char bridge_id[25];
        char stp_str[2];
        char *ptr;
        int fd;
        int nread;
        int stp = 0;
        int need_newline = 1;
        struct dirent *dentry;
        DIR *dir;
        

        snprintf(pathbuf, PATH_MAX, "%s/bridge/bridge_id", name);
        fd = open(pathbuf, O_RDONLY);

        if (fd < 0)
                return -1;

        nread = read(fd, bridge_id, 25);

        close(fd);

        if (nread <= 0)
                return -1;
        
        bridge_id[nread-1] = '\0';

        snprintf(pathbuf, PATH_MAX, "%s/bridge/stp_state", name);
        fd = open(pathbuf, O_RDONLY);

        if (fd < 0)
                return -1;

        nread = read(fd, stp_str, 2);
        
        close(fd);

        if (strcmp(stp_str, "1") == 0)
                stp = 1;
        
        fprintf(stdout, BRCTL_FMT_SSTRING, name, bridge_id, stp ? "yes" : "no" );

        snprintf(pathbuf, PATH_MAX, "/sys/class/net/%s/brif",name);
        
        dir = opendir(pathbuf);

        if (dir) {
                while ((dentry = readdir(dir)) != NULL) {

                        if ((strcmp(dentry->d_name, ".") == 0) ||
                            (strcmp(dentry->d_name, "..") == 0))
                                continue;
                        
                        if (need_newline) {
                                fprintf(stdout, "%s\n", dentry->d_name);
                                need_newline = 0;
                        } else {
                                // 16+24+16+16 = 72
                                fprintf(stdout, "%*c", 57);
                                fprintf(stdout, "%s\n", dentry->d_name);
                        }
                }
        }

        fprintf(stdout, "\n");
        return 0;
        
}

static int cmd_show ()
{
/* Output:
 *bridge name   bridge id               STP enabled     interfaces
 *br0           8000.000000000000       no              eth0
 */
        struct if_nameindex *idx, *indexes;
        if (chdir("/sys/class/net") < 0) {
                fprintf(stderr, "chdir() failed: %s\n", strerror(errno));
                return -1;
        }
        
        indexes = if_nameindex();

        if (!indexes) {
                fprintf(stderr, "if_nameindex() failed: %s\n", strerror(errno));
                return -1;
        }
        /*fprintf(stdout, "bridge name\tbridge id\t\tSTP enabled\tinterfaces");*/
        fprintf(stdout, BRCTL_FMT_STRING, "bridge name", "bridge id",
                "STP enabled", "interfaces");

        for (idx = indexes; idx->if_index; idx++) {
                if (is_if_bridge(idx->if_name) > 0) {
                        cmd_show_if(idx->if_name);
                }
        }

        return 0;

}

/**
 * create_netlink_socket: creates Netlink socket and establishes
 *                        connection to kernel space
 *
 * Returns a handle on success, NULL on error.
 */

static struct nl_sock *create_netlink_socket (void)
{
        struct nl_sock *sock;

        sock = nl_socket_alloc();

        if (!sock) {
                fprintf(stderr, "nl_socket_alloc() failed\n");
                return NULL;
        }
        
        if (nl_connect(sock, NETLINK_ROUTE)) {
                fprintf(stderr, "nl_connect() failed\n");
                goto err;
        }

        return sock;

err:
        if (sock) {
                nl_close(sock);
                nl_socket_free(sock);
        }

        return NULL;
}

/**
 * send_netlink_req: sends the given request to the Netlink layer
 * 
 * Check execute_netlink_cmd() for argument semantics
 *
 * Returns NULL on error, pointer to netlink socket on success
 */

static struct nl_sock *send_netlink_req(struct nl_msg *nl_msg,
                                        struct sockaddr_nl nladdr)
{
        ssize_t nbytes;
        int fd;
        int n;
        struct nlmsghdr *nlmsg = NULL;
        struct pollfd fds[1];
        struct nl_sock *sock = NULL;
        
        nlmsg = nlmsg_hdr(nl_msg);

        sock = create_netlink_socket();

        if (!sock)
                goto err;
        
        fd = nl_socket_get_fd(sock);

        if (fd < 0) {
                fprintf(stderr, "nl_socket_get_fd failed()\n");
                goto err;
        }

        nlmsg->nlmsg_pid = pid;

        nbytes = nl_send_auto_complete(sock, nl_msg);

        if (nbytes < 0) {
                fprintf(stderr, "nl_send_auto_complete() failed()\n");
                goto err;
        }


        //fprintf(stderr, "nbytes = %d\n", nbytes);

        bzero(fds, sizeof(fds));
              
        fds[0].fd = fd;
        fds[0].events = POLLIN;

        n = poll(fds, 1, 2000); /* XXX: define timeout as macro or something */

        if (n < 0) {
                fprintf(stderr, "error in poll(), retcode = %d\n", n);
                goto err;
        }

        if (n == 0) {
                fprintf(stderr, "poll() timeout\n");
                goto err;
        }

        return sock;

err:
        if (sock)
                nl_socket_free(sock);

        return NULL;
}

/**
 * execute_netlink_cmd: send the given message to the Netlink layer
 *                      and process response
 *
 * @nl_msg:      pointer to Netlink message
 * @resp:        pointer to pointer where response buffer will be allocated
 * @resp_buflen: pointer to integer holding the size of the response buffer
 *               on return of the function
 *
 * Returns 0 on success, -1 on error.
 *
 */

static int execute_netlink_cmd (struct nl_msg *nl_msg, struct nlmsghdr **resp,
                                unsigned int *resp_buflen)
{
        struct nlmsghdr *tmp_resp = NULL;
        struct nl_sock *sock = NULL;
        struct pollfd fds[1];
        int len = 0;
        struct sockaddr_nl nladdr = {
                .nl_family = AF_NETLINK,
                .nl_pid    = pid,
                .nl_groups = 0,
        };

        
        bzero(fds, sizeof(fds));

        if (!(sock = send_netlink_req(nl_msg, nladdr)))
                return -1;

        len = nl_recv(sock, &nladdr, (unsigned char **)&tmp_resp, NULL);

        if (len <= 0) {
                fprintf(stderr, "nl_recv() failed\n");
                return -1;
        }

#if 0
        fprintf(stderr, "nl_recv(len) = %d\n", len);
        fprintf(stderr, "nlmsg_datalen = %d\n", nlmsg_datalen(tmp_resp));

        nl_msg_dump(nl_msg, stdout);
#endif
        
        *resp = tmp_resp;
        *resp_buflen = len;
       
        return 0;
}

#if 0
/**
 * netlink_talk: wrapper arround execute_netlink_cmd()
 */

static int netlink_talk (const char *ifname, struct nl_msg *msg,
                         struct nlmsghdr **resp, unsigned int *resp_buflen,
                         int *error)
{
        if (execute_netlink_cmd(msg, resp, resp_buflen) < 0) {
                return -1;
        }

        if (*resp_buflen < 0 || *resp == NULL)
                goto err;

        if ((*resp)->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err;

                err = (struct nlmsgerr *) NLMSG_DATA(*resp);

                if ((*resp)->nlmsg_len < NLMSG_LENGTH(sizeof(*err)))
                        goto err;

                if (err->error < 0) {
                        if (error)
                                *error = err->error;
                        else
                                fprintf(stderr, "Netlink error\n");

                        return -1;
                }
        }

        return 0;
err:
        fprintf(stderr, "Malformed Netlink response message\n");
        return -1;
}
#endif

/**
 * cmd_addbr: creates bridge with given name
 *
 * @ifname: name of the link
 * @error: Netlink error code in case of failure
 *
 * Return 0 on success, -1 on error (@error will be used to store error code)
 *
 */

static int cmd_addbr (const char *ifname, int *error)
{
        
        struct nlmsgerr *err;
        struct nlattr *linkinfo;
        struct ifinfomsg ifinfo;
        struct nl_msg *msg;
        struct nlmsghdr *resp;
        unsigned int resp_len;

        *error = 0;

        dprintf("Creating interface '%s'\n", ifname);

        if (!ifname)
                return -1;

        msg = nlmsg_alloc_simple(RTM_NEWLINK,
                                 NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL);

        if (!msg) {
                fprintf(stderr, "nlmsg_alloc_simple() failed\n");
                return -1;
        }

        if (nlmsg_append(msg, &ifinfo, sizeof(ifinfo), NLMSG_ALIGNTO) < 0) {
                fprintf(stderr, "nlmsg_append() failed (is buffer too small?)\n");
                return -1;
        }

        NETLINK_MSG_PUT(msg, IFLA_IFNAME, (strlen(ifname) + 1), ifname);
        NETLINK_MSG_NEST_START(msg, linkinfo, IFLA_LINKINFO);
        NETLINK_MSG_PUT(msg, IFLA_INFO_KIND, 7, "bridge");
        NETLINK_MSG_NEST_END(msg, linkinfo);

#if 0
        if (!netlink_talk(ifname, msg, &resp, &resp_len, error))
                return -1;
#endif
        if (execute_netlink_cmd(msg, &resp, &resp_len) < 0)
                return -1;
        
        switch (resp->nlmsg_type) {
        case NLMSG_ERROR:
                err = (struct nlmsgerr *)NLMSG_DATA(resp);

                if (resp->nlmsg_len < NLMSG_LENGTH(sizeof(*err)))
                        goto malformed_resp;
                        
                if (err->error < 0) {
                        *error = err->error;
                        return -1;
                }

                break;

        case NLMSG_DONE:
                break;

        default:
                goto malformed_resp;
        }


        return 0;

malformed_resp:
        fprintf(stderr, "Malformed Netlink response message\n");
        return -1;
}

/**
 * cmd_delbr_ioctl: deletes bridge with given name using ioctl(SIOCIFDESTROY)
 *
 * @ifname: name of the link
 *
 * Return 0 on success, -1 on error
 */

static int cmd_delbr_ioctl(const char *ifname)
{
        struct ifreq ifr;
        int sock = -1;

        sock = socket(AF_LOCAL, SOCK_DGRAM, 0);

        if (sock < 0) {
                fprintf(stderr, "socket() failed: %s\n", strerror(errno));
                return -1;
        }
        
        fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);

        if (ioctl(sock, SIOCBRDELBR, ifname) < 0) {
                fprintf(stderr, "cmd_delbr_ioctl(%s) ioctl() failed: %s\n",
                        ifname, strerror(errno));
                return -1;
        }

        close(sock);

        return 0;
}

static void usage (int ret)
{
        fprintf(stdout,
                "Usage: brctl_netlink [commands]\n\n"
                "Commands:\n"
                "addbr  <bridge>               add bridge\n"
                "delbr  <bridge>               delete bridge\n"
                "addif  <bridge> <device>      add interface to bridge\n"
                "delif  <bridge> <device>      delete interface from bridge\n"
                "show                          show a list of bridges\n");

        exit(ret);
}

/**
 * cmd_delbr: deletes bridge with given name
 *
 * @ifname: name of the link
 * @error: Netlink error code in case of failure
 *
 * Return 0 on success, -1 on error (@error will be used to store error code)
 *
 */

static int cmd_delbr(const char *ifname, int *error)
{
        struct nlmsgerr *err;
        struct ifinfomsg ifinfo = { .ifi_family = AF_UNSPEC };
        unsigned int recvbuflen = 0;
        struct nl_msg *nl_msg = NULL;
        struct nlmsghdr *resp = NULL;

        *error = 0;

        dprintf("Deleting interface '%s'\n", ifname);

        nl_msg = nlmsg_alloc_simple(RTM_DELLINK,
                                    NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL);

        if (!nl_msg)
                goto buffer_oom;

        if (nlmsg_append(nl_msg, &ifinfo, sizeof(ifinfo), NLMSG_ALIGNTO) < 0)
                goto buffer_oom;

        if (nla_put(nl_msg, IFLA_IFNAME, strlen(ifname)+1, ifname) < 0)
                goto buffer_oom;

        if (execute_netlink_cmd(nl_msg, &resp, &recvbuflen) < 0)
                return -1;
        
        if (recvbuflen < NLMSG_LENGTH(0) || resp == NULL)
                goto malformed_resp;

        switch (resp->nlmsg_type) {
        case NLMSG_ERROR:
                //fprintf(stderr, "err = %d, nlmsg_len = %d\n", err->error, resp->nlmsg_len);
                err = (struct nlmsgerr *)NLMSG_DATA(resp);

                if (resp->nlmsg_len < NLMSG_LENGTH(sizeof(*err)))
                        goto malformed_resp;

                if (-err->error == EOPNOTSUPP) {
                        /* XXX: was getting EOPNOTSUPP here, going to use ioctl(),
                                should investigate later*/
                        return cmd_delbr_ioctl(ifname);
                }
                
                if (err->error < 0) {
                        fprintf(stderr, "Error deleting network device (errcode = %d)\n", -err->error);
                        *error = err->error;
                        return -1;
                }

                break;

        case NLMSG_DONE:
                break;

        default:
                goto malformed_resp;
        }

        return 0;
        
malformed_resp:
        fprintf(stderr, "Malformed Netlink response message\n");
        return -1;
        
buffer_oom:
        fprintf(stderr, "Netlink buffer is too small\n");
        return -1;
}

static int cmd_addif (const char *brname, const char *ifname, int *code)
{
        struct rtnl_link *link = NULL, *ltap = NULL;
        struct nl_sock *handle = NULL;
        struct nl_cache *cache = NULL;
        int ret = -1;

        handle = nl_socket_alloc();
        if (handle == NULL) {
                fprintf(stderr, "Failed to open Netlink socket\n");
                exit(EXIT_FAILURE);
        }
        
        if (nl_connect(handle, NETLINK_ROUTE) < 0) {
                fprintf(stderr, "connect() failed on Netlink socket\n");
                goto err;
        }
        

        if (rtnl_link_alloc_cache(handle, AF_UNSPEC, &cache) < 0) {
                fprintf(stderr, "Failed to allocate cache\n");
                goto err;
        }

        link = rtnl_link_get_by_name(cache, brname);
        if (!link) {
                fprintf(stderr, "Unable to find the bridge '%s'\n", brname);
                goto err1;
        }

        ltap = rtnl_link_get_by_name(cache, ifname);
        if (!ltap) {
                fprintf(stderr, "Unable to find the interface '%s'\n", ifname);
                goto err2;
        }

        /* XXX: should we check and signal error if "ltap" is already enslaved
           by "link"? */
        if (rtnl_link_enslave(handle, link, ltap) < 0) {
                fprintf(stderr, "Unable to enslave interface '%s' to bridge '%s'\n",
                        ifname, brname);
                goto err3;
        }

        ret = 0;

err3:        
        rtnl_link_put(ltap);
err2:
        rtnl_link_put(link);
err1:
        nl_cache_free(cache);
err:
        nl_socket_free(handle);

        return ret;
}

static int cmd_delif (const char *brname, const char *ifname, int *code)
{
        struct nl_sock *handle = NULL;
        struct nl_cache *cache = NULL;
        struct rtnl_link *ltap;
        int ret = -1;

        handle = nl_socket_alloc();
        if (handle == NULL) {
                fprintf(stderr, "Failed to open Netlink socket\n");
                exit(EXIT_FAILURE);
        }

        if (nl_connect(handle, NETLINK_ROUTE) < 0) {
                fprintf(stderr, "connect() failed on Netlink socket\n");
                goto err;
        }

        if (rtnl_link_alloc_cache(handle, AF_UNSPEC, &cache) < 0) {
                fprintf(stderr, "Failed to allocate cache\n");
                goto err;
        }

        ltap = rtnl_link_get_by_name(cache, ifname);
        if (!ltap) {
                fprintf(stderr, "Unable to find the interface '%s'\n", ifname);
                goto err1;
        }

        if (rtnl_link_release(handle, ltap) < 0) {
                fprintf(stderr, "Unable to release interface '%s' from bridge '%s'\n",
                        ifname, brname);
                goto err2;
        }

        ret = 0;
err2:
        rtnl_link_put(ltap);
err1:
        nl_cache_free(cache);
err:
        nl_socket_free(handle);

        return ret;
}

int main (int argc, char **argv)
{
        int err = 0;

        if (argc == 1) {
                usage(EXIT_SUCCESS);
        }
        
        pid = getpid();

        if (strcmp(argv[1], "addbr") == 0) {
                if (argc != 3) {
                        usage(EXIT_FAILURE);
                }

                if (cmd_addbr(argv[2], &err) < 0) {
                        fprintf(stderr, "Failed to add bridge\n");

                        if (err != 0 || errno != 0) {
                                fprintf(stderr, "code: %d, errno: %s\n", err, strerror(errno));
                        }

                        exit(EXIT_FAILURE);
                }

                exit(EXIT_SUCCESS);
        }

        if (strcmp(argv[1], "delbr") == 0) {
                if (argc != 3) {
                        usage(EXIT_FAILURE);
                }

                if (cmd_delbr(argv[2], &err) < 0) {
                        fprintf(stderr, "Failed to delete bridge\n");

                        if (err != 0 || errno != 0) {
                                fprintf(stderr, "code: %d, errno: %s\n", err, strerror(errno));
                        }

                        exit(EXIT_FAILURE);
                }

                exit(EXIT_SUCCESS);
        }

        if (strcmp(argv[1], "show") == 0) {
                if (argc != 2) {
                        usage(EXIT_FAILURE);
                }

                return cmd_show();
        }

        if (strcmp(argv[1], "addif") == 0) {
                if (argc != 4) {
                        usage(EXIT_FAILURE);
                }
                
                if (cmd_addif(argv[2], argv[3], &err) < 0) {
                        fprintf(stderr, "Failed to add interface\n");

                        if (err != 0 || errno != 0) {
                                fprintf(stderr, "code: %d, errno: %s\n", err, strerror(errno));
                        }

                        exit(EXIT_FAILURE);
                }

                exit(EXIT_SUCCESS);
        }

        if (strcmp(argv[1], "delif") == 0) {
                if (argc != 4) {
                        usage(EXIT_FAILURE);
                }

                if (cmd_delif(argv[2], argv[3], &err) < 0) {
                        fprintf(stderr, "Failed to delete interface\n");

                        if (err != 0 || errno != 0) {
                                fprintf(stderr, "code: %d, errno: %s\n", err, strerror(errno));
                        }

                        exit(EXIT_FAILURE);
                }

                exit(EXIT_SUCCESS);
        }

        usage(EXIT_FAILURE);

        return 0;
#if 0        
        if (cmd_delbr("br0", &err) < 0) {
                fprintf(stderr, "cmd_delbr() failed, errcode = %x\n", err);
                return -1;
        }
        
        if (cmd_addbr("br0", &err) < 0) {
                fprintf(stderr, "cmd_addbr() failed, errcode = %d\n", err);
                return -1;
        }

        if (cmd_delbr("br0", &err) < 0) {
                fprintf(stderr, "cmd_delbr() failed, errcode = %d\n", err);
                return -1;
        }

        return 0;
#endif
}
