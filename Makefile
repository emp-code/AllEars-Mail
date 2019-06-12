CC=gcc
CFLAGS=-g -march=native -pipe -Wall -Werror=array-bounds -Werror=format-overflow=0 -Werror=format -Werror=implicit-function-declaration -Werror=implicit-int -Werror=incompatible-pointer-types -Wno-comment -Wno-switch -Wno-unused-variable
objects = main.o http.o https.o smtp.o Includes/Base64.o Includes/Brotli.o Includes/SixBit.o

ae-mail: $(objects)
	$(CC) $(CFLAGS) -o ae-mail $(objects) -lsodium -lmbedtls -lmbedcrypto -lmbedx509 -lbrotlienc -lm

main: main.c http.h https.h

Includes/Base64.o: Includes/Base64.c
Includes/Brotli.o: Includes/Brotli.c
Includes/SixBit.o: Includes/SixBit.c

http.o: http.c
https.o: https.c Includes/Base64.h Includes/SixBit.h

.PHONY: clean
clean:
	-rm $(objects)
