typedef struct my_address {
  int sockfd;
  struct in_addr sin;
} t_my_address;

typedef struct tunnel {
  char name[MAX_NAME];
  struct in_addr local;
  struct in_addr remote;
  unsigned short ttl;
  char if_up[MAX_PATH];
  char if_down[MAX_PATH];
  long keepalive_count;
  unsigned long transitions;
  int period;
  int retries;
  time_t last_keepalive;
} t_tunnel;

struct gre_header {
  unsigned short flags;
  unsigned short ptype;
};

union control_data {
  struct cmsghdr cmsg;
  u_char data[CMSG_SPACE(sizeof(struct in_pktinfo))];
};
