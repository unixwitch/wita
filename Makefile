CC		= cc
LINT		= lint
CPPFLAGS	= -D_XOPEN_SOURCE=500 -D__EXTENSIONS__
CFLAGS		= -xO0 -g -xc99=%none
LDFLAGS		=
LINTFLAGS	= -axsm -u -errtags=yes -s -Xc99=%none -errsecurity=core
LIBS		= -lsocket -lnsl -lrt

OBJS	= main.o pdns.o server.o group.o config.o
SRCS	= main.c pdns.c server.c group.c config.c
PROG	= wita

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $(PROG) $(LIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

lint:
	$(LINT) $(LINTFLAGS) $(SRCS)

clean:
	rm -f $(OBJS) $(PROG)

.KEEP_STATE:
