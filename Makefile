olich: olich.c
	$(CC) olich.c -o olich -Wall -Wextra -pedantic --std=c89 -m64 -g

clean:
	rm olich
