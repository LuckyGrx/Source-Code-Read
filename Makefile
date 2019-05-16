all: httpd

httpd: httpd.c
	gcc -g -o httpd -W -Wall httpd.c -lpthread

clean:
	rm httpd
