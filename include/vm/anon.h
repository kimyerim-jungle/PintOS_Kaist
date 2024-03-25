#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

struct anon_page {
    /* Initiate the contets of the page */
	vm_initializer *init;
	enum vm_type type;
	void *aux;
	/* Initiate the struct page and maps the pa to the va 
	 * 페이지를 초기화하고 물리주소를 가상주소에 매핑합니다.
	 */
	
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
