all: sum smartloader

sum:
	gcc -m32 -no-pie -nostdlib -o sum sum.c

smartloader:
	gcc -m32 -o smartloader smartloader.c

clean:
	-@rm -f sum smartloader
