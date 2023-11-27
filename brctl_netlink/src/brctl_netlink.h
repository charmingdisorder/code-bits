#ifndef _BRCTL_NETLINK_H_
#define _BRCTL_NETLINK_H_

#define BRCTL_FMT_STRING "%-16s%-24s%-16s%-16s\n"

#define BRCTL_FMT_SSTRING "%-16s%-24s%-16s"

#define BRCTL_FMT_STRING_SIZ (16+24+16+IFNAMSIZ)

#define dprintf(...)                                                    \
        do { if (debug) fprintf(stderr, __VA_ARGS__); } while (0)

#define NETLINK_MSG_NEST_START(msg, container, attrtype) \
do { \
        container = nla_nest_start(msg, attrtype); \
        if (!container) { \
                fprintf(stderr, "Allocated Netlink buffer is too small"); \
                return -1; \
        } \
} while (0)

#define NETLINK_MSG_NEST_END(msg, container) \
        do { nla_nest_end(msg, container); } while (0)

#define NETLINK_MSG_PUT(msg, attrtype, datalen, data) \
do { \
        const void *dataptr = data;                                \
        if (dataptr && nla_put(msg, attrtype, datalen, dataptr) < 0) {  \
                fprintf(stderr, "Allocated Netlink buffer is too small"); \
                return -1; \
        } \
} while (0)

#define NETLINK_MSG_APPEND(msg, datalen, dataptr) \
do { \
        if (nlmsg_append(msg, dataptr, datalen, NLMSG_ALIGNTO) < 0) {   \
                fprintf(stderr, "Allocated Netlink buffer is too small"); \
                return -1; \
        } \
} while (0)

#endif
