olich: olich.c
	$(CC) olich.c -o olich -Wall -Wextra -pedantic --std=c89 

clean:
	rm olich
