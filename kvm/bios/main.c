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

//! default BIOS rom path
#define BIOS_PATH "/usr/share/seabios/bios.bin"

#define BIOS_MEM_SIZE   0x00020000  /* 128KB */
#define BIOS_LEGACY_ADDR  0x000e0000
#define BIOS_SHADOW_ADDR  0xfffe0000

#define RAM1_BASE 0x00000000
#define RAM1_SIZE 0x000A0000 /* 640KB */
#define RAM2_BASE 0x000C0000 /* VGA BIOS Base Address */
#define RAM2_SIZE 0x00020000 /* 128KB */

#define SERIAL_IO_TX  0x0402

int kvm_set_user_memory_region (
		int vmfd, unsigned long long guest_phys_addr,
		unsigned long long memory_size, unsigned long long userspace_addr) {
	static unsigned int kvm_usmem_slot = 0;

	struct kvm_userspace_memory_region usmem;
	usmem.slot = kvm_usmem_slot++;
	usmem.guest_phys_addr = guest_phys_addr;
	usmem.memory_size = memory_size;
	usmem.userspace_addr = userspace_addr;
	usmem.flags = 0;
	return ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &usmem);
}

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
	} else if (fstat.st_size > BIOS_MEM_SIZE) {     /* 128KB */
		printf("bios rom size must less than 128KB\n");
		exit(1);
	}
	int bios_size = fstat.st_size;
	printf("bios_size: %ld\n", fstat.st_size);

	//! rom memory pass to kvm
	void *bios_mem;
	ret = posix_memalign(&bios_mem, 4096, BIOS_MEM_SIZE);		/* 128KB */
	if (ret == -1) {
		printf("bios_mem cannot allocated\n");
		exit(1);
	}
	//! get binary using mmap
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
	/* make IRQ */
	ret = ioctl(vmfd, KVM_CREATE_IRQCHIP);
	if (ret != 0) {
		printf("err KVM_CREATE_IRQCHIP\n");
		exit(1);
	}
	/* make TIMER */
	ret = ioctl(vmfd, KVM_CREATE_PIT);
	if (ret != 0) {
		printf("err KVM_CREATE_PIT\n");
		exit(1);
	}

	/* map bios to vm (legacy) */
	ret = kvm_set_user_memory_region(vmfd, BIOS_LEGACY_ADDR, BIOS_MEM_SIZE, (unsigned long long)bios_mem);
	if (ret == -1) {
		printf("err bios: KVM_SET_USER_MEMORY_REGION(legacy)\n");
		exit(1);
	}
	/* map bios to vm (shadow) */
	ret = kvm_set_user_memory_region(vmfd, BIOS_SHADOW_ADDR, BIOS_MEM_SIZE, (unsigned long long)bios_mem);
	if (ret == -1) {
		printf("err bios: KVM_SET_USER_MEMORY_REGION(shadow)\n");
		exit(1);
	}

	///*** ram setting ***///
	void *addr1 = mmap(0, RAM1_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (addr1 == MAP_FAILED) {
		printf("ram1 mmap faild\n");
		exit(1);
	}
  ret = kvm_set_user_memory_region(vmfd, RAM1_BASE, RAM1_SIZE, (unsigned long long)addr1);
  if (ret == -1) {
		printf("err ram1: KVM_SET_USER_MEMORY_REGION\n");
		exit(1);
	}
	void *addr2 = mmap(0, RAM2_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (addr2 == MAP_FAILED) {
		printf("ram2 mmap faild\n");
		exit(1);
	}
  ret = kvm_set_user_memory_region(vmfd, RAM2_BASE, RAM2_SIZE, (unsigned long long)addr2);
  if (ret == -1) {
		printf("err ram2: KVM_SET_USER_MEMORY_REGION\n");
		exit(1);
	}

	/* make VCPU */
	int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
	if (vcpufd == -1) {
		printf("err KVM_CREATE_VCPU\n");
	}
	size_t mmap_size = ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, NULL);
	if (mmap_size == 0) {
		printf("err KVM_GET_VCPU_MMAP_SIZE\n");
		exit(1);
	}
	struct kvm_run *run = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE,
			MAP_SHARED, vcpufd, 0);
	if (run == MAP_FAILED) {
		printf("err mmap vcpu\n");
		exit(1);
	}

	/* struct kvm_sregs sregs;  /1* segment regs init *1/ */
	/* ioctl(vcpufd, KVM_GET_SREGS, &sregs); */
	/* sregs.cs.base = 0; */
	/* sregs.cs.selector = 0; */
	/* ioctl(vcpufd, KVM_SET_SREGS, &sregs); */

	/* struct kvm_regs regs = {  /1* status regs init *1/ */
	/* 	.rip = 0x0, */
	/* 	.rflags = 0x02, /1* RFLAGS初期状態 *1/ */
	/* }; */
	/* ioctl(vcpufd, KVM_SET_REGS, &regs); */

	/* run VM */
	while (1) {
		ret = ioctl(vcpufd, KVM_RUN, 0);
		if (ret == -1) {
			printf("err KVM_RUN\n");
		}
		/* dump_regs(vcpufd); */

		switch (run->exit_reason) {
			case KVM_EXIT_IO:
				/* io_handle(run); */
				if (run->io.port == SERIAL_IO_TX) {
					switch (run->io.direction) {
					case KVM_EXIT_IO_IN:
						for (int i = 0; i < run->io.count; i++) {
							switch (run->io.size) {
							case 1:
								*(unsigned char *)((unsigned char *)run + run->io.data_offset) = 0;
								break;
							case 2:
								*(unsigned short *)((unsigned char *)run + run->io.data_offset) = 0;
								break;
							case 4:
								*(unsigned short *)((unsigned char *)run + run->io.data_offset) = 0;
								break;
							}
							run->io.data_offset += run->io.size;
						}
						break;

					case KVM_EXIT_IO_OUT:
						for (int i = 0; i < run->io.count; i++) {
							if (run->io.size != 1) {
								printf("err serial: Undefined IO size\n");
								exit(1);
							}
							putchar(*(char *)((unsigned char *)run + run->io.data_offset));
							run->io.data_offset += run->io.size;
						}
						break;
					}
				}

				break;

			default:
				fflush(stdout);
				exit(1);
		}
	}

	return 0;
}

