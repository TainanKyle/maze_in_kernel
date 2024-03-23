#include <linux/module.h>	// included for all kernel modules
#include <linux/kernel.h>	// included for KERN_INFO
#include <linux/init.h>		// included for __init and __exit macros
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/sched.h>	// task_struct requried for current_uid()
#include <linux/cred.h>		// for current_uid();
#include <linux/slab.h>		// for kmalloc/kfree
#include <linux/uaccess.h>	// copy_to_user
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include "maze.h"

#define WALL '#'
#define PATH '.'
#define START_SYM 'S'
#define END_SYM 'E'
#define CUR_SYM '*'

static dev_t devnum;
static struct cdev c_dev;
static struct class *clazz;
static DEFINE_MUTEX(m_mutex);

static maze_t *mazes[_MAZE_MAXUSER];
static bool process_maze[_MAZE_MAXUSER];
static pid_t maze_pid[_MAZE_MAXUSER];
static coord_t *maze_cur[_MAZE_MAXUSER];
static maze_t *visiteds[_MAZE_MAXUSER];


static int mazemod_dev_open(struct inode *i, struct file *f) {
	// printk(KERN_INFO "mazemod: device opened.\n");
	return 0;
}

static int mazemod_dev_close(struct inode *i, struct file *f) {
	// printk(KERN_INFO "mazemod: device closed.\n");
	int maze_idx = -1;
	mutex_lock(&m_mutex);
	for(int i = 0; i < _MAZE_MAXUSER; i++) {
		if(maze_pid[i] == current->pid) {
			maze_idx = i;
			break;
		}
	}

	// release all the process related resources
	if(maze_idx >= 0) {
		kfree(mazes[maze_idx]);
		mazes[maze_idx] = NULL;
		kfree(maze_cur[maze_idx]);
		maze_cur[maze_idx] = NULL;
		kfree(visiteds[maze_idx]);
		visiteds[maze_idx] = NULL;
		process_maze[maze_idx] = false;
		maze_pid[maze_idx] = 0;
	}
	mutex_unlock(&m_mutex);
	return 0;
}

static ssize_t mazemod_dev_read(struct file *f, char __user *buf, size_t len, loff_t *off) {
	// printk(KERN_INFO "mazemod: read %zu bytes @ %llu.\n", len, *off);
	// check if maze is created
	int maze_idx = -1;
	mutex_lock(&m_mutex);
	for(int i = 0; i < _MAZE_MAXUSER; i++) {
		if(maze_pid[i] == current->pid) {
			maze_idx = i;
			break;
		}
	}
	mutex_unlock(&m_mutex);
	if(maze_idx < 0) return -EBADFD;

	maze_t *maze = mazes[maze_idx];
	char *maze_byte = kmalloc(maze->w * maze->h, GFP_KERNEL);
	if (!maze_byte) return -ENOMEM;

	for (int y = 0; y < maze->h; ++y) {
		for (int x = 0; x < maze->w; ++x) {
			maze_byte[(y * maze->w) + x] = (maze->blk[y][x] == WALL) ? 1 : 0;
		}
	}
	
	if (copy_to_user(buf, maze_byte, maze->w * maze->h)) {
		kfree(maze_byte);
		return -EBUSY;
	}
	kfree(maze_byte);
	return maze->w * maze->h;
}

static ssize_t mazemod_dev_write(struct file *f, const char __user *buf, size_t len, loff_t *off) {
	// printk(KERN_INFO "mazemod: write %zu bytes @ %llu.\n", len, *off);
	int maze_idx = -1;
    mutex_lock(&m_mutex);
    for(int i = 0; i < _MAZE_MAXUSER; i++) {
        if(maze_pid[i] == current->pid) {
            maze_idx = i;
            break;
        }
    }
    mutex_unlock(&m_mutex);
    if(maze_idx < 0) return -EBADFD;

    if (len % sizeof(coord_t) != 0) return -EINVAL;
    coord_t *move_arr = kmalloc(len, GFP_KERNEL);
    if (!move_arr) return -ENOMEM;

    if (copy_from_user(move_arr, buf, len)) {
        kfree(move_arr);
        return -EBUSY;
    }

	mutex_lock(&m_mutex);
	maze_t *maze = mazes[maze_idx];
	coord_t *cur = maze_cur[maze_idx];
	maze->blk[cur->y][cur->x] = PATH;
	for(int i = 0; i < len / sizeof(coord_t); i++) {
		coord_t move = move_arr[i];
		if (maze->blk[(cur->y + move.y)][(cur->x + move.x)] != WALL) {
			cur->x += move.x;
			cur->y += move.y;
		}
	}
	maze->blk[cur->y][cur->x] = CUR_SYM;
	mutex_unlock(&m_mutex);
	kfree(move_arr);
	return len;
}

bool reachable(maze_t *maze, int start_x, int start_y, int end_x, int end_y, maze_t *visited) {
	if (maze->blk[start_y][start_x] == WALL || visited->blk[start_y][start_x] == '1') return false;
	visited->blk[start_y][start_x] = '1';
	if (start_x == end_x && start_y == end_y) return true;

	if (reachable(maze, start_x + 1, start_y, end_x, end_y, visited) ||
		reachable(maze, start_x - 1, start_y, end_x, end_y, visited) ||
		reachable(maze, start_x, start_y + 1, end_x, end_y, visited) ||
		reachable(maze, start_x, start_y - 1, end_x, end_y, visited)) 
		return true;
	
	return false;
}

static long mazemod_dev_ioctl(struct file *fp, unsigned int cmd, unsigned long arg) {
	int target = -1, maze_idx = -1;
	// check if maze is created
	mutex_lock(&m_mutex);
	for(int i = 0; i < _MAZE_MAXUSER; i++) {
		if(maze_pid[i] == current->pid) {
			maze_idx = i;
			break;
		}
	}
	mutex_unlock(&m_mutex);

	switch(cmd) {
		case MAZE_CREATE: {
			if (maze_idx >= 0) return -EEXIST;
			mutex_lock(&m_mutex);
			
			coord_t *size = kmalloc(sizeof(coord_t), GFP_KERNEL);
			if (copy_from_user(size, (coord_t *)arg, sizeof(coord_t))) {
				kfree(size);
				mutex_unlock(&m_mutex);
				return -EBUSY;
			}
			if (!size || (size->x) <= 0 || (size->y) <= 0) {
				mutex_unlock(&m_mutex);
				return -EINVAL;
			} 

			// find available space for new maze
			int maze_count = 0;
			for (int i = 0; i < _MAZE_MAXUSER; ++i) {
				if (process_maze[i]) maze_count++;
				else {
					target = i;
					break;
				}
			}
			if (maze_count == _MAZE_MAXUSER) {
				mutex_unlock(&m_mutex);
				return -ENOMEM;
			} 

			maze_t *maze = kmalloc(sizeof(maze_t), GFP_KERNEL);
			maze_t *visited = kmalloc(sizeof(maze_t), GFP_KERNEL);
			maze_cur[target] = kmalloc(sizeof(coord_t), GFP_KERNEL);
			if (!maze || !maze_cur[target] || !visited) {
				kfree(maze);
				kfree(size);
				kfree(visited);
				kfree(maze_cur[target]);
				mutex_unlock(&m_mutex);
				return -ENOMEM;
			} 

			// create maze layout
			maze->w = size->x;
			maze->h = size->y;
			for (int y = 0; y < size->y; ++y) {
				for (int x = 0; x < size->x; ++x) {
					if (y == 0 || y == size->y - 1 || x == 0 || x == size->x - 1)
						maze->blk[y][x] = WALL;
					else {
						if (get_random_u32() % 4 == 0) maze->blk[y][x] = WALL;
						else maze->blk[y][x] = PATH;
					}
				}
			}

			// check if there is a valid path
			do {
				for (int i = 0; i < _MAZE_MAXY; ++i) {
					for (int j = 0; j < _MAZE_MAXX; ++j) {
						visited->blk[i][j] = '0';
					}
				}
				do {
					maze->sx = get_random_u32() % size->x;
					maze->sy = get_random_u32() % size->y;
				} while (maze->blk[maze->sy][maze->sx] == WALL);
				do {
					maze->ex = get_random_u32() % size->x;
					maze->ey = get_random_u32() % size->y;
				} while (maze->blk[maze->ey][maze->ex] == WALL || (maze->sx == maze->ex && maze->sy == maze->ey));
			} while (reachable(maze, maze->sx, maze->sy, maze->ex, maze->ey, visited) == false);

			maze_cur[target]->x = maze->sx;
			maze_cur[target]->y = maze->sy;

			maze->blk[maze->sy][maze->sx] = START_SYM;
			maze->blk[maze->ey][maze->ex] = END_SYM;
			maze->blk[maze_cur[target]->y][maze_cur[target]->x] = CUR_SYM;

			mazes[target] = maze;
			visiteds[target] = visited;
			maze_pid[target] = current->pid;
			process_maze[target] = true;
			printk(KERN_INFO "- create maze done.\n");
			kfree(size);
			mutex_unlock(&m_mutex);
			return 0;
		}

		case MAZE_RESET: {
			if (maze_idx < 0) return -ENOENT;
			mutex_lock(&m_mutex);
			// move the current position to start position
			maze_t *maze = mazes[maze_idx];
			maze->blk[(maze->sy)][(maze->sx)] = CUR_SYM;
			coord_t *cur = maze_cur[maze_idx];
			maze->blk[(cur->y)][(cur->x)] = PATH;
			cur->x = maze->sx;
			cur->y = maze->sy;
			mutex_unlock(&m_mutex);
			return 0;
		}

		case MAZE_DESTROY: {
			if (maze_idx < 0) return -ENOENT;
			mutex_lock(&m_mutex);
			// delete the maze that the process made
			kfree(mazes[maze_idx]);
			mazes[maze_idx] = NULL;
			kfree(maze_cur[maze_idx]);
			maze_cur[maze_idx] = NULL;
			kfree(visiteds[maze_idx]);
			visiteds[maze_idx] = NULL;
			process_maze[maze_idx] = false;
			maze_pid[maze_idx] = 0;
			mutex_unlock(&m_mutex);
			return 0;
		}

		case MAZE_GETSIZE: {
			if (maze_idx < 0) return -ENOENT;
			coord_t *size = kmalloc(sizeof(coord_t), GFP_KERNEL);
			if (!size) {
				kfree(size);
				return -ENOMEM;
			}
			// get the size of maze
			size->x = mazes[maze_idx]->w;
			size->y = mazes[maze_idx]->h;
			if (copy_to_user((coord_t *)arg, size, sizeof(coord_t))) return -EBUSY;
			kfree(size);
			return 0;
		}

		case MAZE_MOVE: {
			if (maze_idx < 0) return -ENOENT;
			mutex_lock(&m_mutex);
			coord_t *move = kmalloc(sizeof(coord_t), GFP_KERNEL);
			if (!move) {
				kfree(move);
				mutex_unlock(&m_mutex);
				return -ENOMEM;
			}
			if (copy_from_user(move, (coord_t *)arg, sizeof(coord_t))) {
				kfree(move);
				mutex_unlock(&m_mutex);
				return -EBUSY;
			} 

			// update the current position
			maze_t *maze = mazes[maze_idx];
			coord_t *cur = maze_cur[maze_idx];
			if (maze->blk[(cur->y + move->y)][(cur->x + move->x)] != WALL) {
				maze->blk[(cur->y)][(cur->x)] = PATH;
				cur->y += move->y;
				cur->x += move->x;
				maze->blk[(cur->y)][(cur->x)] = CUR_SYM;
			}
			mutex_unlock(&m_mutex);
			kfree(move);
			return 0;
		}

		case MAZE_GETPOS: {
			if (maze_idx < 0) return -ENOENT;
			coord_t *cur = maze_cur[maze_idx];
			if (copy_to_user((coord_t *)arg, cur, sizeof(coord_t))) return -EBUSY;
			return 0;
		}

		case MAZE_GETSTART: {
			if (maze_idx < 0) return -ENOENT;
			coord_t *start_pos = kmalloc(sizeof(coord_t), GFP_KERNEL);
			if (!start_pos) {
				kfree(start_pos);
				return -ENOMEM;
			}

			start_pos->x = mazes[maze_idx]->sx;
			start_pos->y = mazes[maze_idx]->sy;
			if (copy_to_user((coord_t *)arg, start_pos, sizeof(coord_t))) return -EBUSY;
			kfree(start_pos);
			return 0;
		}

		case MAZE_GETEND: {
			if (maze_idx < 0) return -ENOENT;
			coord_t *end_pos = kmalloc(sizeof(coord_t), GFP_KERNEL);
			if (!end_pos) {
				kfree(end_pos);
				return -ENOMEM;
			}

			end_pos->x = mazes[maze_idx]->ex;
			end_pos->y = mazes[maze_idx]->ey;
			if (copy_to_user((coord_t *)arg, end_pos, sizeof(coord_t))) return -EBUSY;
			kfree(end_pos);
			return 0;
		}
	}
	
	printk(KERN_INFO "mazemod: ioctl cmd=%u arg=%lu.\n", cmd, arg);
	return 0;
}

static const struct file_operations mazemod_dev_fops = {
	.owner = THIS_MODULE,
	.open = mazemod_dev_open,
	.read = mazemod_dev_read,
	.write = mazemod_dev_write,
	.unlocked_ioctl = mazemod_dev_ioctl,
	.release = mazemod_dev_close
};

static int mazemod_proc_read(struct seq_file *m, void *v) {
	mutex_lock(&m_mutex);
	int i;
    for (i = 0; i < _MAZE_MAXUSER; ++i) {
		if(process_maze[i]) {
			maze_t *maze = mazes[i];
			coord_t *cur = maze_cur[i];
			if (maze != NULL) {
				seq_printf(m, "#%02d: pid %d - [%d x %d]: (%d, %d) -> (%d, %d) @ (%d, %d)\n",
						   i, maze_pid[i], maze->w, maze->h, maze->sx, maze->sy, maze->ex, maze->ey, cur->x, cur->y);
				for (int y = 0; y < maze->h; ++y) {
					seq_printf(m, "- %03d: ", y);
					for (int x = 0; x < maze->w; ++x) {
						seq_printf(m, "%c", maze->blk[y][x]);
					}
					seq_printf(m, "\n");
				}
				seq_printf(m, "\n");
			}
		} else {
			seq_printf(m, "#%02d: vacancy\n\n", i);
		}
    }
	mutex_unlock(&m_mutex);
    return 0;
}

static int mazemod_proc_open(struct inode *inode, struct file *file) {
	return single_open(file, mazemod_proc_read, NULL);
}

static const struct proc_ops mazemod_proc_fops = {
	.proc_open = mazemod_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static char *mazemod_devnode(const struct device *dev, umode_t *mode) {
	if(mode == NULL) return NULL;
	*mode = 0666;
	return NULL;
}

static int __init mazemod_init(void)
{
	// create char dev
	if(alloc_chrdev_region(&devnum, 0, 1, "updev") < 0)
		return -1;
	if((clazz = class_create("upclass")) == NULL)
		goto release_region;
	clazz->devnode = mazemod_devnode;
	if(device_create(clazz, NULL, devnum, NULL, "maze") == NULL)
		goto release_class;
	cdev_init(&c_dev, &mazemod_dev_fops);
	if(cdev_add(&c_dev, devnum, 1) == -1)
		goto release_device;

	// create proc
	proc_create("maze", 0, NULL, &mazemod_proc_fops);

	for(int i = 0; i < _MAZE_MAXUSER; i++) {
		mazes[i] = NULL;
		process_maze[i] = false;
		maze_pid[i] = 0;
		visiteds[i] = NULL;
	}
	mutex_init(&m_mutex);

	printk(KERN_INFO "mazemod: initialized.\n");
	return 0;    // Non-zero return means that the module couldn't be loaded.

release_device:
	device_destroy(clazz, devnum);
release_class:
	class_destroy(clazz);
release_region:
	unregister_chrdev_region(devnum, 1);
	return -1;
}

static void __exit mazemod_cleanup(void)
{
	remove_proc_entry("maze", NULL);

	cdev_del(&c_dev);
	device_destroy(clazz, devnum);
	class_destroy(clazz);
	unregister_chrdev_region(devnum, 1);
	mutex_destroy(&m_mutex);

	printk(KERN_INFO "mazemod: cleaned up.\n");
}

module_init(mazemod_init);
module_exit(mazemod_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyle");