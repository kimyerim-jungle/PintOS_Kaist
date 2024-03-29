/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "vm/uninit.h"
#include "vm/anon.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "threads/mmu.h"
#include "kernel/list.h"
#include "lib/string.h"

struct list frame_table;
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
    vm_anon_init();
    vm_file_init();
#ifdef EFILESYS /* For project 4 */
    pagecache_init();
#endif
    register_inspect_intr();
    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */

    list_init(&frame_table);
    lock_init(&vm_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
    int ty = VM_TYPE(page->operations->type);
    switch (ty)
    {
    case VM_UNINIT:
        return VM_TYPE(page->uninit.type);
    default:
        return ty;
    }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux)
{

    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;
    // printf("thcu tid %d\n", thread_current()->tid);
    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL)
    {
        /* TODO: Create the page, fetch the initialier according to the VM type,
         * TODO: and then create "uninit" page struct by calling uninit_new. You
         * TODO: should modify the field after calling the uninit_new. */

        // struct page *new_page = (struct page *)malloc(sizeof(struct page));
        struct page *new_page = palloc_get_page(0);
        //  new_page->va = palloc_get_page(PAL_USER);
        //   struct uninit_page un_page;

        if (VM_TYPE(type) == VM_ANON)
        {
            uninit_new(new_page, upage, init, type, aux, anon_initializer);
        }
        else if (VM_TYPE(type) == VM_FILE)
        {
            uninit_new(new_page, upage, init, type, aux, file_backed_initializer);
        }
        new_page->writable = writable;
        /* TODO: Insert the page into the spt. */
        return spt_insert_page(spt, new_page);

        // return true;
    }
err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt, void *va)
{
    struct page *page = NULL;
    /* TODO: Fill this function. */
    struct hash *hash = &spt->hash_table;

    page = (struct page *)palloc_get_page(0);
    page->va = pg_round_down(va);
    struct hash_elem *e = hash_find(hash, &page->h_elem);
    if (e == NULL)
    {
        palloc_free_page(page);
        return NULL;
    }
    palloc_free_page(page);
    page = hash_entry(e, struct page, h_elem);

    return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
                     struct page *page UNUSED)
{
    int succ = false;
    /* TODO: Fill this function. */
    struct hash *hash = &spt->hash_table;

    lock_acquire(&vm_lock);
    if (hash_insert(hash, &page->h_elem) != NULL)
    {
        lock_release(&vm_lock);
        return succ;
    }
    lock_release(&vm_lock);
    succ = true;
    return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
    lock_acquire(&vm_lock);
    vm_dealloc_page(page);
    lock_release(&vm_lock);
    return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */
    lock_acquire(&vm_lock);
    struct list_elem *e = list_pop_front(&frame_table);
    lock_release(&vm_lock);
    victim = list_entry(e, struct frame, f_elem);
    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */
    if (swap_out(victim->page))
    {
        return victim;
    }

    return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
    struct frame *frame; // 정적 선언
    /* TODO: Fill this function. */
    void *kva;
    struct page *page = NULL;

    // frame = (struct frame *)malloc(sizeof(struct frame));
    frame = palloc_get_page(0);
    kva = palloc_get_page(PAL_USER);
    if (kva == NULL)
        PANIC("todo"); // vm_evict_frame();

    frame->kva = kva;
    frame->page = page;

    lock_acquire(&vm_lock);
    list_push_back(&frame_table, &frame->f_elem);
    lock_release(&vm_lock);

    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);
    return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
                         bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    /* TODO: Validate the fault */
    /* TODO: Your code goes here */
    // if (user)
    if (is_kernel_vaddr(addr))
    {
        return false;
    }
    if (addr == NULL)
    {
        return false;
    }

    if (not_present)
    {
        page = spt_find_page(spt, addr);

        if (page == NULL)
        {
            return false;
        }
        if (write == 1 && page->writable == 0)
            return false;
        return vm_do_claim_page(page);
    }
    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
    destroy(page);
    free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
    struct page *page = NULL;
    /* TODO: Fill this function */
    struct supplemental_page_table *spt = &thread_current()->spt;

    page = spt_find_page(spt, va);
    if (page == NULL)
        return false;
    // page->va = va;

    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page)
{
    struct frame *frame = vm_get_frame();

    /* Set links */
    if (page->frame != NULL)
    {
        return false;
    }
    frame->page = page;
    page->frame = frame;

    /* TODO: Insert page table entry to map page's VA to frame's PA. */
    struct thread *cur = thread_current();
    if (pml4_get_page(cur->pml4, pg_round_down(page->va)) || !pml4_set_page(cur->pml4, pg_round_down(page->va), pg_round_down(frame->kva), page->writable))
    {
        // printf("pml4 set false\n");
        return false;
    }

    return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
    hash_init(&spt->hash_table, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED)
{
    struct hash *src_hash = &src->hash_table;
    struct hash *dst_hash = &dst->hash_table;
    struct hash_iterator i;

    hash_first(&i, src_hash);
    // printf("copy tid %d\n", thread_current()->tid);
    // lock_acquire(&vm_lock);
    while (hash_next(&i))
    {
        struct page *p = hash_entry(hash_cur(&i), struct page, h_elem);
        if (p == NULL)
            return false;
        enum vm_type type = page_get_type(p);
        // printf("type %d  p->type %d\n", type, p->operations->type);
        struct page *child;

        if (p->operations->type == VM_UNINIT)
        {
            // uninit_new(child, p->va, NULL, p->operations->type, NULL, NULL);
            if (!vm_alloc_page_with_initializer(type, p->va, p->writable, p->uninit.init, p->uninit.aux))
                return false;
        }
        else
        {
            if (!vm_alloc_page(type, p->va, p->writable))
                return false;
            if (!vm_claim_page(p->va))
                return false;

            child = spt_find_page(dst, p->va);
            memcpy(child->frame->kva, p->frame->kva, PGSIZE);
        }
    }

    return true;
}

void hash_elem_destroy(struct hash_elem *e, void *aux UNUSED)
{
    struct page *p = hash_entry(e, struct page, h_elem);
    destroy(p);
    free(p);
}
/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
    struct hash *hash = &spt->hash_table;

    // lock_acquire(&vm_lock);
    // hash_destroy(hash, hash_elem_destroy);
    // // lock_release(&vm_lock);
    hash_clear(hash, hash_elem_destroy);
}

unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
    const struct page *p = hash_entry(p_, struct page, h_elem);
    return hash_bytes(&p->va, sizeof p->va);
}

bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
    const struct page *a = hash_entry(a_, struct page, h_elem);
    const struct page *b = hash_entry(b_, struct page, h_elem);

    return a->va < b->va;
}