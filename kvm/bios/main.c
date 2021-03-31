#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include<string.h>

#include<linux/kvm.h>
#include<sys/ioctl.h>

#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/mman.h>

//! default BIOS
#define BIOS_PATH "/usr/share/seabios/bios.bin"


int main(int argc, char **argv) {
	int ret;
	char *rom = BIOS_PATH;
	/* char *rom = argv[1]; */
	/* if (argc != 2) { */
	/* 	printf("usage: ./kvm_bios [rom]"); */
	/* } */

	///*** bios rom setting ***///
	int biosfd = open(rom, O_RDONLY);
	if (biosfd == -1) {
		printf("cannot open rom(bios)\n");
		exit(1);
	}

	struct stat fstat;
	if (stat(rom, &fstat) != 0) {
		printf("cannot get file stat(length)\n");
		exit(1);
	} else if (fstat.st_size > 0x00020000) {     /* 128KB */
		printf("bios rom size must less than 128KB\n");
		exit(1);
	}
	int bios_size = fstat.st_size;
	printf("bios_size: %ld\n", fstat.st_size);

	void *bios_mem;
	ret = posix_memalign(&bios_mem, 4096, 0x00020000);		/* 128KB */
	if (ret == -1) {
		printf("bios_mem cannot allocated\n");
		exit(1);
	}

	uint8_t *bios_bin = mmap(0, bios_size, PROT_READ, MAP_SHARED, biosfd, 0);
	if (bios_bin == MAP_FAILED) {
		printf("mmap failed\n");
		exit(1);
	}
	
	memcpy(bios_mem, bios_bin, bios_size);
	///*** bios rom setting fin ***///
	///*** bios_mem: bios rom for kvm ***///
	///*** bios_size: bios rom size ***///
	



	///*** KVM code ***///
  int kvmfd = open("/dev/kvm", O_RDWR);
	if (kvmfd < 0) {
		printf("err open /dev/kvm\n");
		exit(1);
	}
	
	//! make VM
  int vmfd = ioctl(kvmfd, KVM_CREATE_VM, 0);

	//! ROM
	uint8_t *mem = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
			MAP_SHARED|MAP_ANONYMOUS|MAP_NORESERVE,
			-1, 0);
	memcpy(mem, rom_bin, fstat.st_size); 
	struct kvm_userspace_memory_region region = {
		.guest_phys_addr = 0,
		.memory_size = 0x1000,				///////// ???????????????????
		.userspace_addr = (uint64_t)mem
	};
	ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region); /* memory setting */

	/* make VCPU */
	int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
	size_t mmap_size = ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, NULL);
	struct kvm_run *run = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE,
			MAP_SHARED, vcpufd, 0);

	struct kvm_sregs sregs;  /* segment regs init */
	ioctl(vcpufd, KVM_GET_SREGS, &sregs);
	sregs.cs.base = 0;
	sregs.cs.selector = 0;
	ioctl(vcpufd, KVM_SET_SREGS, &sregs);

	struct kvm_regs regs = {  /* status regs init */
		.rip = 0x0,
		.rflags = 0x02, /* RFLAGS初期状態 */
	};
	ioctl(vcpufd, KVM_SET_REGS, &regs);

	/* run VM */
	uint8_t is_running = 1;
	while (is_running) {
		ioctl(vcpufd, KVM_RUN, NULL);

		switch (run->exit_reason) { /* VM Exit */
			case KVM_EXIT_HLT:  /* HLT */
				/* printf("KVM_EXIT_HLT\n"); */
				is_running = 0;
				fflush(stdout);
				break;

			case KVM_EXIT_IO: /* IO action */
				if (run->io.port == 0x01
						&& run->io.direction == KVM_EXIT_IO_OUT) {
					putchar(*(char *)((unsigned char *)run + run->io.data_offset));
					fflush(stdout);
				}
		}
	}
	printf("\nfin\n");

	return 0;
}
