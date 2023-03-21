CFLAGS=-c -Wall -O0 
LIBS = 

all: epd_image 

epd_image: main.o 
	$(CC) main.o $(LIBS) -o epd_image 

main.o: main.c
	$(CC) $(CFLAGS) main.c

clean:
	rm -rf *.o epd_image
