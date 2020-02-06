#define DO_DEBUG 1
#define CONFIG_LINE 1024
#define MAX_PATH 256
#define MAX_NAME 16
#define UDP_PORT 25162
#define STAT_FILE "/var/lib/gre_keepalived.stats"
#define DEFAULT_CONFIG "/etc/gre_tunnels.cfg"
#define dstaddr(x) (&(((struct in_pktinfo*) (CMSG_DATA(x)) )-> ipi_addr))
