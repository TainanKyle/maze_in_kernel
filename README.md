# Maze In Kernel

### Introduction
This work implemented a character device as a kernel module that handles read, write, and ioctl operations. 
The character device implement several required features to construct and run a maze in the Linux kernel.

1. The module has to automatically create one device named `maze` in the `/dev` filesystem. Each process can create only one maze at the same time. Once a maze is created, the process can get the size of the maze, the position of the player (current position, start position, end position) in the maze, and the layout of the maze. When the device is opened by a process, the process can interact with the module using `read`, `write`, and `ioctl` operations. Details of each of the commands are summarized as follows.
2. The `ioctl` interface. The `ioctl` command supports the following commands. The definition of the commands can be obtained from the header file `maze.h`.
  - `MAZE_CREATE`: Create a new maze. The maze layout and the player’s positions are all randomly generated. It is ensured there is a valid path from the start position to the end position.
  - `MAZE_RESET`: Reset the position of the player to the start position.
  - `MAZE_DESTROY`: Destroy a maze if it has been created.
  - `MAZE_GETSIZE`: Get the dimension of the maze.
  - `MAZE_MOVE`: Move the player position on the maze.
  - `MAZE_GETPOS`: Get the player’s position on the maze.
  - `MAZE_GETSTART`: Get the start position of the maze.
  - `MAZE_GETEND`: Get the end position of the maze.
3. The `read` interface. A process can obtain the layout of a maze using the read operation.
4. The `write` interface. In addition to the `MAZE_MOVE` command in the `ioctl` interface, a process can move the current positoin of the player in the maze in a batch manner.
5. The `/proc/maze` interface. The content of this file is the status of all the mazes created by user space processes.

### Demo
1. Run `mazetest 0`
```
### case - cat /proc/maze ###
#00: vacancy

#01: vacancy

#02: vacancy
```
2. Run `mazetest 3`
```
### case - create a maze and then move ###
OP: MAZE_CREATE 33 7
- create maze done.
#00: pid 77 - [33 x 7]: (29, 3) -> (15, 1) @ (29, 3)
- 000: #################################
- 001: #.#...........#E..#...#...#.....#
- 002: #.#####.#####.#.#.#.#.#.#.#.#####
- 003: #.....#.#...#.#.#.#.#...#.#..*..#
- 004: #####.#.#.###.#.#.#.#####.#####.#
- 005: #.......#.......#...#...........#
- 006: #################################

#01: vacancy

#02: vacancy

OP: MAZE_MOVE -1 0
#00: pid 77 - [33 x 7]: (29, 3) -> (15, 1) @ (28, 3)
OP: MAZE_MOVE 1 0
#00: pid 77 - [33 x 7]: (29, 3) -> (15, 1) @ (29, 3)
OP: MAZE_MOVE 0 -1
#00: pid 77 - [33 x 7]: (29, 3) -> (15, 1) @ (29, 3)
OP: MAZE_MOVE 0 1
#00: pid 77 - [33 x 7]: (29, 3) -> (15, 1) @ (29, 3)
OP: MAZE_RESET
```

3. Run `mazetest 5`
```
### case - create a maze and then read ###
OP: MAZE_CREATE 35 7
- Create maze done.
#00: pid 122 - [35 x 7]: (27, 1) -> (31, 3) @ (27, 1)
- 000: ###################################
- 001: #.#...#...............#...#*......#
- 002: #.#.#.#.#######.#####.#.#.#####.#.#
- 003: #...#.#.......#...#.#.#.#.....#E#.#
- 004: #####.###########.#.#.#.#####.###.#
- 005: #.................#.....#.........#
- 006: ###################################

#01: vacancy

#02: vacancy

OP: MAZE_GETSIZE
OP: MAZE_GETSTART
OP: MAZE_GETEND
OP: MAZE_GETPOS
- Size [35 x 7]: (27, 1) -> (31, 3) @ (27, 1)
000: ╔═╦═══╦═══════════════╦═══╦═══════╗
001: ║ ║   ║               ║   ║*      ║
002: ║ ║ ║ ║ ══════╗ ══╦═╗ ║ ║ ╚═══╗ ║ ║
003: ║   ║ ║       ║   ║ ║ ║ ║     ║E║ ║
004: ╠═══╝ ╚═══════╩══ ║ ║ ║ ╠════ ╚═╝ ║
005: ║                 ║     ║         ║
006: ╚═════════════════╩═════╩═════════╝
```

4. Run `mazetest 6`
```
### case - create a maze and then read ###
OP: MAZE_CREATE 31 5
- Create maze done.
#00: pid 130 - [31 x 5]: (23, 1) -> (9, 1) @ (23, 1)
- 000: ###############################
- 001: #...#....E#.......#...#*......#
- 002: ###.#.###.###.###.#.#.#####.#.#
- 003: #.....#.......#.....#.......#.#
- 004: ###############################

#01: vacancy

#02: vacancy

OP: MAZE_GETSIZE
OP: MAZE_GETSTART
OP: MAZE_GETEND
OP: MAZE_GETPOS
- Size [31 x 5]: (23, 1) -> (9, 1) @ (23, 1)
000: ╔═══╦═════╦═══════╦═══╦═══════╗
001: ║   ║    E║       ║   ║*      ║
002: ╠══ ║ ╔══ ╚══ ╔══ ║ ║ ╚════ ║ ║
003: ║     ║       ║     ║       ║ ║
004: ╚═════╩═══════╩═════╩═══════╩═╝
### case - create a maze and then read + randomwalk ###
- Batch move operations sent
OP: MAZE_GETPOS
- Check position
000: ╔═══╦═════╦═══════╦═══╦═══════╗
001: ║   ║    E║       ║   ║S      ║
002: ╠══ ║ ╔══ ╚══ ╔══ ║ ║ ╚════ ║ ║
003: ║     ║       ║     ║       ║*║
004: ╚═════╩═══════╩═════╩═══════╩═╝
- Check PASSED!
```
