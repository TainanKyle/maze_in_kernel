
obj-m     += mazemod.o
ccflags-y += -DEXPORT_SYMTAB

all:
	make -C ../dist/modulebuild M=$(PWD) modules
	cp mazemod.ko ../rootfs/modules

clean:
	rm -f maze
	make -C ../dist/modulebuild M=$(PWD) clean

# find ./ | cpio -o -H newc | bzip2 -c > ../dist/rootfs.cpio.bz2
