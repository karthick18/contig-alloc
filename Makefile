##Makefile for the Contiguous Allocator 1.0 which is now working quite okay.
CC=gcc
MYDEFINES=-DDEBUG -DOOPS
VMALLOC_DEFINES=CUSTOM_VMALLOC
CFLAGS = -Wall -O2 -D__KERNEL__ -DMODULE $(MYDEFINES) -D$(VMALLOC_DEFINES)  -I/usr/src/linux/include
OBJECTS = contiguous.o  my_allocator.o
USER_FLAGS=-DDEBUG
TARGET=contiguous_allocation_1.o
USER_TARGET=user_mmap
MODULE=contiguous_allocation_1
INSMOD=/sbin/insmod
RMMOD=/sbin/rmmod
ARGS=percent_high=3
DEVICE_NAME=contiguous
DEVICE_NODE=/dev/contiguous
LD=/usr/bin/ld

ifeq ($(VMALLOC_DEFINES),CUSTOM_VMALLOC)
 OBJECTS += my_vmalloc.o
endif

all: $(TARGET) $(USER_TARGET)

$(TARGET): $(OBJECTS) 
	$(LD) -r -o $@ $^

contiguous.o: contiguous.c 
	$(CC) -c $^ $(CFLAGS) -o $@

my_vmalloc.o: my_vmalloc.c
	$(CC) $(CFLAGS) -c $^ -o $@

my_allocator.o: my_allocator.c
	$(CC) $(CFLAGS) -c $^  -o $@

user_mmap: user_mmap.o
	$(CC) -o $@ $^ ;\
	echo "Type \"make install\" " ;\
	echo "Finally Type \"./user_mmap\" to test the contiguous allocator"

user_mmap.o: user_mmap.c
	$(CC) $(USER_FLAGS) -c $^ -o $@ -Wall -I. 

mknod:
	major=`cat /proc/devices | tr -s ' ' | awk -F " " "\\$$2 == \"$(DEVICE_NAME)\" { print \\$$1 }"` ;\
	if test -n "$${major}" -a "$${major}" != " " ; then \
	echo Major Nr=$${major} ;\
	rm -rf $(DEVICE_NODE) ;\
	mknod $(DEVICE_NODE) c $${major} 0 ;\
	else echo "Module $(MODULE) was not installed" ; make remove ; \
	fi

install:
	$(INSMOD) $(TARGET) $(ARGS) ;\
	if test $$? -eq 0 ; then make mknod ; fi


remove:
	$(RMMOD) $(MODULE)
	rm -rf $(DEVICE_NODE)

clean:
	rm -f *.o *~
