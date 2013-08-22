CC	:= gcc
CFLAGS	:= -Wall
LIBS	:= -lpthread -lSDL -ldl -fPIC

all: PURE libs strip

debug: PUREd libsd

PURE: PURE.c plugin.c libPURE.c usbstring.c
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS)
	
PUREd: PURE.c plugin.c libPURE.c usbstring.c
	sed -i 's/verbose=0/verbose=3/' libPURE.c
	$(CC) $(CFLAGS) -g $(LDFLAGS) $^ -o $@ $(LIBS)
	sed -i 's/verbose=3/verbose=0/' libPURE.c
	
libs: 
	${CC} ${CFLAGS} ${LDFLAGS} -shared -fPIC ./plugins/libps3.c -o ./plugins/libps3.so
	${CC} ${CFLAGS} ${LDFLAGS} -shared -fPIC ./plugins/libkeyboard.c -o ./plugins/libkeyboard.so
	
libsd:
	${CC} ${CFLAGS} -g ${LDFLAGS} -shared -fPIC ./plugins/libps3.c -o ./plugins/libps3.so
	${CC} ${CFLAGS} -g ${LDFLAGS} -shared -fPIC ./plugins/libkeyboard.c -o ./plugins/libkeyboard.so

strip:
	strip PURE ./plugins/*.so
	
clean:
	rm -f PURE ./plugins/*.so
