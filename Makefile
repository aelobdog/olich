olich: src/olich.c
	$(CC) src/olich.c -o bin/olich -Wall -Wextra -pedantic --std=c89 

clean:
	rm bin/olich
