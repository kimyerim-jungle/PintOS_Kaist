/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct hash swap_table;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

unsigned anon_page_hash(const struct hash_elem *p_, void *aux UNUSED);
bool anon_page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

static struct lock swap_lock;

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
    /* TODO: Set up the swap_disk. */
    hash_init(&swap_table, anon_page_hash, anon_page_less, NULL);
    lock_init(&swap_lock);
    swap_disk = disk_get(1, 1); // swap

    disk_sector_t swap_size = disk_size(swap_disk) / 8;
    printf("init start\n");
    for (disk_sector_t i = 0; i < swap_size; i++)
    {
        struct slot *insert_disk = (struct slot *)malloc(sizeof(struct slot));
        insert_disk->used = 0;
        insert_disk->index = i;
        if (insert_disk == NULL)
            printf("NULL\n");
        lock_acquire(&swap_lock);
        hash_insert(&swap_table, &insert_disk->swap_elem);
        lock_release(&swap_lock);
    }
    printf("init done\n");
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
    /* Set up the handler */
    page->operations = &anon_ops;

    struct anon_page *anon_page = &page->anon;

    anon_page->slot_idx = -1;

    return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
    struct anon_page *anon_page = &page->anon;
    struct slot *slot;
    disk_sector_t page_slot_index = anon_page->slot_idx;

    struct hash_iterator i;
    printf("swap in start\n");
    hash_first(&i, &swap_table);
    lock_acquire(&swap_lock);
    while (hash_next(&i))
    {
        slot = hash_entry(hash_cur(&i), struct slot, swap_elem);
        if (slot->index == page_slot_index)
        {
            for (int i = 0; i < 8; i++)
                disk_read(swap_disk, page_slot_index * 8 + i, kva + DISK_SECTOR_SIZE * i);
        }
        slot->page = NULL;
        slot->used = 0;
        anon_page->slot_idx = -1;
        lock_release(&swap_lock);
        printf("swap in succ\n");
        return true;
    }
    printf("swap in fail\n");
    lock_release(&swap_lock);
    return false;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
    if (page == NULL)
        return false;
    struct anon_page *anon_page = &page->anon;
    struct slot *slot;
    struct hash_iterator i;
    printf("swap out start\n");
    hash_first(&i, &swap_table);
    lock_acquire(&swap_lock);
    while (hash_next(&i))
    {
        slot = hash_entry(hash_cur(&i), struct slot, swap_elem);
        if (slot->used == 0)
        {
            for (int i = 0; i < 8; i++)
            {
                disk_write(swap_disk, slot->index * 8 + i, page->va + DISK_SECTOR_SIZE * i);
            }

            anon_page->slot_idx = slot->index;
            slot->page = page;
            slot->used = 1;
            page->frame->page = NULL;
            page->frame = NULL;
            pml4_clear_page(thread_current()->pml4, page->va);
            lock_release(&swap_lock);
            printf("swap out succ\n");
            return true;
        }
    }
    lock_release(&swap_lock);
    PANIC("full swap disk");
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
    struct anon_page *anon_page = &page->anon;
}

unsigned anon_page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
    const struct slot *p = hash_entry(p_, struct slot, swap_elem);
    return hash_bytes(&p->page->va, sizeof p->page->va);
}

bool anon_page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
    const struct slot *a = hash_entry(a_, struct slot, swap_elem);
    const struct slot *b = hash_entry(b_, struct slot, swap_elem);

    return a->page->va < b->page->va;
}
