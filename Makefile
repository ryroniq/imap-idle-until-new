CC_ARGS =

-include config.mk

all: imap-idle-until-new

imap-idle-until-new: imap-idle-until-new.c
	$(CC) $(CC_ARGS) -o $@ $<

clean:
	rm -f *.o imap-idle-until-new

