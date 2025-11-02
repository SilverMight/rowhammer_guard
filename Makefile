
obj-m += anvil.o
anvil-objs := anvil_main.o dram_mapping.o intel_dram_mapping.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
