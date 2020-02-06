CC = gcc

gre_keepalived: gre_keepalived.c util.o
	$(CC) -o $@ $< util.o $(CFLAGS) $(LDFLAGS) $(LIBS)

util.o: util.c
	$(CC) -c $< $(CFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -vf *.o gre_keepalived
