#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/init.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
// #include "lib/user/syscall.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/synch.h"
#include "threads/palloc.h"
#include "lib/string.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

#include "vm/vm.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const char *file);
int wait(tid_t);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);

int insert_file_fdt(struct file *file);
int process_add_file(struct file *f);
struct file_descriptor *find_file_descriptor(int fd);
/* System call.
 *
 * 사용자 프로세스가 커널 기능에 액세스하기를 원할 때마다 시스템 호출을 호출합니다.
 * 스켈레톤 시스템 호출 핸들러입니다. 현재는 메시지만 출력하고 사용자 프로세스를 종료합니다.
 * 이 프로젝트의 2부에서는 시스템 호출에 필요한 다른 모든 작업을 수행하기 위해 코드를 추가합니다.

 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * 이전 시스템 호출 서비스는 인터럽트 핸들러에서 처리했습니다
 * (예: 리눅스의 경우 int 0x80). 그러나 x86-64에서는 시스템 호출을
 * 요청하기 위한 효율적인 경로, 즉 'syscall' 명령을 제공한다.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual.
 *
 * syscall 명령은 모델에서 값을 읽음으로써 작동합니다
 * MSR(Specific Register). 자세한 내용은 설명서 참조 */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

struct file_descriptor *find_file_descriptor(int fd)
{
    struct list *fd_table = &thread_current()->fd_table;
    ASSERT(fd_table != NULL);
    ASSERT(fd > 1);
    if (list_empty(fd_table))
        return NULL;
    struct file_descriptor *file_descriptor;
    struct list_elem *e = list_begin(fd_table);
    ASSERT(e != NULL);
    while (e != list_tail(fd_table))
    {
        file_descriptor = list_entry(e, struct file_descriptor, fd_elem);
        if (file_descriptor->fd == fd)
        {
            return file_descriptor;
        }
        e = list_next(e);
    }
    return NULL;
}

/* An open file. */
struct file
{
    struct inode *inode; /* File's inode. */
    off_t pos;           /* Current position. */
    bool deny_write;     /* Has file_deny_write() been called? */
};

void syscall_init(void)
{
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
                            ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL.
     * 인터럽트 서비스 반올림은 인터럽트 서비스를 제공해서는 안 됩니다
     * syscall_entry가 userland 스택을 커널 모드 스택으로 스왑할 때까지. 따라서 FLAG_FL을 마스킹했습니다.*/
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

    lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
    // TODO: Your implementation goes here.

    struct thread *t = thread_current();
    t->tf = *f;
#ifdef VM
    t->rsp_stack = f->rsp;
#endif
    // int size = palloc_init();

    // 주소가 유호한지 확인
    if (!is_user_vaddr(f->rsp))
    {
        printf("isnotvaddr\n");
        thread_exit();
    }

    else if (f->rsp > KERN_BASE || f->rsp < 0)
    {
        printf("smaller\n");
        thread_exit();
    }

    int addr = (f->rsp + 8);
    if (!is_user_vaddr(addr) || (addr > KERN_BASE || addr < 0))
    {
        printf("third condition\n");
        thread_exit();
    }

    // addr = f->R.rsi;

    switch (f->R.rax)
    {
    case SYS_HALT:
        halt();
        break;

    case SYS_EXIT:
        exit(f->R.rdi);
        break;

    case SYS_FORK:
        f->R.rax = fork(f->R.rdi, f);
        break;

    case SYS_EXEC:
        f->R.rax = exec(f->R.rdi);
        break;

    case SYS_WAIT:
        f->R.rax = wait(f->R.rdi);
        break;

    case SYS_CREATE:
        f->R.rax = create(f->R.rdi, f->R.rsi);
        break;

    case SYS_REMOVE:
        f->R.rax = remove(f->R.rdi);
        break;

    case SYS_OPEN:
        f->R.rax = open(f->R.rdi);
        break;

    case SYS_FILESIZE:
        f->R.rax = filesize(f->R.rdi);
        break;

    case SYS_READ:
        f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
        break;

    case SYS_WRITE:
        f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
        break;

    case SYS_SEEK:
        seek(f->R.rdi, f->R.rsi);
        break;

    case SYS_TELL:
        f->R.rax = tell(f->R.rdi);
        break;

    case SYS_CLOSE:
        close(f->R.rdi);
        break;

    case SYS_MMAP:
        f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
        break;

    case SYS_MUNMAP:
        munmap(f->R.rdi);
        break;

    default:
        break;
    }
}

// 핀토스 종료
void halt(void)
{
    power_off();
}

// 현재 유저 프로그램 종료 (status를 반환함)
void exit(int status)
{
    thread_current()->exit_status = status;
    thread_exit();
}

tid_t fork(const char *thread_name, struct intr_frame *f)
{
    return process_fork(thread_name, f);
}

// 현재 프로세스를 file로 바꿈
int exec(const char *file)
{

    if (pml4_get_page(thread_current()->pml4, file) == NULL || file == NULL || !is_user_vaddr(file))
        exit(-1);

    char *file_in_kernel;
    file_in_kernel = palloc_get_page(PAL_ZERO);

    if (file_in_kernel == NULL)
        exit(-1);
    strlcpy(file_in_kernel, file, PGSIZE);

    if (process_exec(file_in_kernel) == -1)
        return -1;
}

// 자식 프로세스 tid가 끝날때까지 기다림 & 자식프로세스의 status를 반환함
int wait(tid_t t)
{
    return process_wait(t);
}

// file이라는 파일을 만들고, 성공시 true 반환
// 파일 생성은 해당 파일을 열지는 않음. 열기 위해서는 open을 사용
// 메인 쓰레드의 fd_table 리스트에 할당
bool create(const char *file, unsigned initial_size)
{
    // 가상메모리 주소에 해당하는 물리메모리 주소를 확인하고, 커널의 가상메모리 주소를 반환함
    if (!is_user_vaddr(file))
    {
        exit(-1);
    }
    if (pml4_get_page(thread_current()->pml4, file) == NULL || file == NULL || *file == '\0')
        exit(-1);

    lock_acquire(&filesys_lock);
    bool success = filesys_create(file, initial_size);
    lock_release(&filesys_lock);
    return success;
}

// file이라는 파일을 삭제
// 성공시 true 반환
bool remove(const char *file)
{
    // 가상메모리 주소에 해당하는 물리메모리 주소를 확인하고, 커널의 가상메모리 주소를 반환함
    if (pml4_get_page(thread_current()->pml4, file) == NULL || file == NULL || !is_user_vaddr(file) || *file == '\0')
        exit(-1);
    lock_acquire(&filesys_lock);
    bool success = filesys_remove(file);
    lock_release(&filesys_lock);

    return success;
}

// file이라는 파일을 연다
// fd반환
int open(const char *file)
{
    if (thread_current()->last_created_fd >= 126)
    {
        exit(126);
    }
    if (!is_user_vaddr(file))
        exit(-1);

    if (pml4_get_page(thread_current()->pml4, file) == NULL || file == NULL)
        exit(-1);

    lock_acquire(&filesys_lock);
    struct file *open_file = filesys_open(file);
    int fd = -1;
    if (open_file == NULL)
    {
        lock_release(&filesys_lock);
        return fd;
    }
    fd = process_add_file(open_file);
    if (fd == -1)
        file_close(open_file);
    lock_release(&filesys_lock);
    return fd;
}

void close(int fd)
{

    struct file_descriptor *close_fd = find_file_descriptor(fd);
    if (close_fd == NULL)
        return -1;
    file_close(close_fd->file);
    list_remove(&close_fd->fd_elem);
    free(close_fd);
}

int filesize(int fd)
{
    struct file_descriptor *file_desc = find_file_descriptor(fd);
    if (file_desc == NULL)
        return -1;
    return file_length(file_desc->file);
}

int read(int fd, void *buffer, unsigned size)
{
    if (buffer == NULL || fd < 0 || !is_user_vaddr(buffer))
        exit(-1);
    struct page *p = spt_find_page(&thread_current()->spt, buffer);

    off_t buff_size;
    if (fd == 0)
    {
        return input_getc();
    }
    else if (fd == NULL || fd == 1)
    {
        // exit(-1);
        return -1;
    }
    else
    {
        struct file_descriptor *read_fd = find_file_descriptor(fd);
        if (read_fd == NULL)
            return -1;
        if (p && !p->writable)
            exit(-1);

        lock_acquire(&filesys_lock);
        buff_size = file_read(read_fd->file, buffer, size);
        lock_release(&filesys_lock);
    }
    return buff_size;
}

int write(int fd, const void *buffer, unsigned size)
{
    if (buffer == NULL || !is_user_vaddr(buffer) || fd < 0)
        exit(-1);
    struct page *p = spt_find_page(&thread_current()->spt, buffer);
    if (p == NULL)
        exit(-1);
    if (fd == 1)
    {
        putbuf(buffer, size);
        return size;
    }
    else if (fd < 0 || fd == NULL)
    {
        exit(-1);
    }
    struct file_descriptor *write_fd = find_file_descriptor(fd);
    if (write_fd == NULL)
        return -1;
    if (write_fd->file && write_fd->file->deny_write)
        exit(-1);
    lock_acquire(&filesys_lock);
    off_t write_size = file_write(write_fd->file, buffer, size);
    lock_release(&filesys_lock);
    return write_size;
}

void seek(int fd, unsigned position)
{
    struct file_descriptor *seek_fd = find_file_descriptor(fd);
    if (seek_fd == NULL)
        return -1;
    return file_seek(seek_fd->file, position);
}

unsigned tell(int fd)
{
    struct file_descriptor *tell_fd = find_file_descriptor(fd);
    if (tell_fd == NULL)
        return -1;
    return file_tell(tell_fd->file);
}

int process_add_file(struct file *f)
{
    struct thread *curr = thread_current();
    struct file_descriptor *new_fd = malloc(sizeof(struct file_descriptor));

    // curr에 있는 fd_table의 fd를 확인하기 위한 작업

    curr->last_created_fd += 1;
    new_fd->fd = curr->last_created_fd;
    new_fd->file = f;
    list_push_back(&curr->fd_table, &new_fd->fd_elem);

    return new_fd->fd;
}

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{
    // 파일의 시작점(offset)이 page-align되지 않았을 때
    if (offset % PGSIZE != 0)
    {
        return NULL;
    }
    // 가상 유저 page 시작 주소가 page-align되어있지 않을 때
    /* failure case 2: 해당 주소의 시작점이 page-align되어 있는지 & user 영역인지 & 주소값이 null인지 & length가 0이하인지*/
    if (pg_round_down(addr) != addr || is_kernel_vaddr(addr) || addr == NULL || (long long)length <= 0)
    {
        return NULL;
    }
    // 매핑하려는 페이지가 이미 존재하는 페이지와 겹칠 때(==SPT에 존재하는 페이지일 때)

    if (spt_find_page(&thread_current()->spt, addr))
    {
        return NULL;
    }

    // 콘솔 입출력과 연관된 파일 디스크립터 값(0: STDIN, 1:STDOUT)일 때
    if (fd == 0 || fd == 1)
    {
        exit(-1);
    }
    // 찾는 파일이 디스크에 없는경우
    struct file *target = find_file_descriptor(fd)->file;
    if (target == NULL)
    {
        return NULL;
    }

    return do_mmap(addr, length, writable, target, offset);
}
void munmap(void *addr)
{
    do_munmap(addr);
}