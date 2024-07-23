#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "kernel/hash.h"

struct page;
enum vm_type;

struct anon_page
{
    /* Initiate the contets of the page */
    enum vm_type type;
    void *va;
    int slot_idx;
};

struct slot
{
    struct hash_elem swap_elem;
    int used; // 사용중 1, 사용가능 0
    int index;
    struct page *page;
};

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
