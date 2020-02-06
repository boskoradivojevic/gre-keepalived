#!/bin/sh
killall -SIGUSR1 gre_keepalived
cat /var/lib/gre_keepalived.stats
