/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "lib/string.h"
#include "userprog/process.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

static bool lazy_load_file(struct page *page, void *aux);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

struct file
{
    struct inode *inode; /* File's inode. */
    off_t pos;           /* Current position. */
    bool deny_write;     /* Has file_deny_write() been called? */
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
    /* Set up the handler */
    page->operations = &file_ops;

    struct file_page *file_page = &page->file;

    file_page->type = type;
    file_page->va = kva;

    return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
    struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
    struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
    struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
        struct file *file, off_t offset)
{
    // offset ~ length

    void *ret = addr;
    struct file *open_file = file_reopen(file);

    if (open_file == NULL)
        return NULL;

    size_t read_byte = file_length(file) < length ? file_length(file) : length;
    size_t zero_byte = PGSIZE - read_byte % PGSIZE;

    ASSERT ((read_byte + zero_byte) % PGSIZE == 0);
	ASSERT (pg_ofs (addr) == 0);
	ASSERT (offset % PGSIZE == 0);

    while (read_byte > 0 || zero_byte > 0)
    {
        size_t page_read_bytes = read_byte < PGSIZE ? read_byte : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;
        // printf("ofs %d\n", offset);
        void *aux = NULL;
        struct necessary_info *nec = (struct necessary_info *)malloc(sizeof(struct necessary_info));
        nec->file = file;
        nec->ofs = offset;
        nec->read_byte = page_read_bytes;
        nec->zero_byte = page_zero_bytes;
        aux = nec;

        if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable,lazy_load_segment , aux))
            return NULL;

        read_byte -= page_read_bytes;
        zero_byte -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }
    // printf("bbb\n");
    return ret;
}

/* Do the munmap */
// void do_munmap(void *addr)
// {
//     // 연결 끊기

//     struct page *page = spt_find_page(&thread_current()->spt, addr);
//     if (page == NULL)
//         printf("p NULL\n");
//     // struct frame *frame = page->frame;

//     page->frame = NULL;
//     // palloc_free_page(frame->kva);
//     // palloc_free_page(frame);

//     struct file *file = file_reopen(page->file.file);

//     // 더티 페이지 디스크에 작성해주기, 작성 후 더티 비트 0으로 변경
//     if (pml4_is_dirty(&thread_current()->pml4, addr))
//     {
//         file_write(file, addr, file->pos);
//         pml4_set_dirty(&thread_current()->pml4, addr, 0);
//     }

//     // 페이지테이블 보조 테이블에서 삭제
//     spt_remove_page(&thread_current()->spt, page);
// }

void do_munmap(void *addr)
{
    while(true)
    {
		struct thread *curr = thread_current();
		struct page *find_page = spt_find_page(&curr->spt, addr);
		
		if (find_page == NULL) {
    		return NULL;
    	}
    
        struct necessary_info* nec = (struct necessary_info*)find_page->uninit.aux;
        if(pml4_is_dirty(curr->pml4, find_page->va))
        {
            file_write_at(nec->file,addr,nec->read_byte,nec->ofs);
            pml4_set_dirty(curr->pml4,find_page->va,0);
        }
        pml4_clear_page(curr->pml4,find_page->va);
        addr += PGSIZE;
    }
}

static bool
lazy_load_file(struct page *page, void *aux)
{
    /* TODO: Load the segment from the file */
    /* TODO: This called when the first page fault occurs on address VA. */
    /* TODO: VA is available when calling this function. */

    struct necessary_info *nec = (struct necessary_info *)aux;

    void *kpage = page->frame->kva;

    file_seek(nec->file, nec->ofs);
    page->file.file = nec->file;
    /* Load this page. */
    size_t read = file_read(nec->file, kpage, nec->read_byte);
    // if (read != (int)nec->read_byte)
    //{
    //     printf("nec->read_byte %d  succ read %d\n", nec->read_byte, read);
    //     palloc_free_page(kpage);
    //     printf("file read fail\n");
    //     return false;
    // }
    memset(kpage + nec->read_byte, 0, nec->zero_byte);
    // file_seek(nec->file, nec->ofs);
    return true;
}
