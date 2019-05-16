all: httpd

httpd: httpd.c
	gcc -o httpd -W -Wall httpd.c -lpthread

clean:
	rm httpd
