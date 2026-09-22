CC_ARGS =
LIBS = -lssl -lcrypto

-include config.mk

all: imap-idle-until-new

imap-idle-until-new: imap-idle-until-new.c
	$(CC) $(CC_ARGS) $(LIBS) -o $@ $<

clean:
	rm -f *.o imap-idle-until-new

